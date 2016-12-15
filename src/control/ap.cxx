// xmlauto.cxx - a more flexible, generic way to build autopilots
//
// Written by Curtis Olson, started January 2004.
//
// Copyright (C) 2004  Curtis L. Olson  - http://www.flightgear.org/~curt
//
// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Library General Public
// License as published by the Free Software Foundation; either
// version 2 of the License, or (at your option) any later version.
//
// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Library General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
//


#include "python/pyprops.hxx"

#include <math.h>
#include <stdlib.h>

#include <string>
#include <sstream>
using std::string;
using std::ostringstream;

#include "comms/logging.hxx"
#include "util/wind.hxx"

#include "ap.hxx"


FGPIDVelComponent::FGPIDVelComponent( string config_path ):
    ep_n_1( 0.0 ),
    edf_n_1( 0.0 ),
    edf_n_2( 0.0 ),
    u_n_1( 0.0 ),
    desiredTs( 0.00001 ),
    elapsedTime( 0.0 )
{
    size_t pos;

    component_node = pyGetNode(config_path, true);
    
    // enable
    pyPropertyNode node = component_node.getChild("enable", true);
    string enable_prop = node.getString("prop");
    enable_value = node.getString("value");
    honor_passive = node.getBool("honor_passive");
    pos = enable_prop.rfind("/");
    if ( pos != string::npos ) {
	string path = enable_prop.substr(0, pos);
	enable_attr = enable_prop.substr(pos+1);
	printf("path = %s attr = %s\n", path.c_str(), enable_attr.c_str());
	enable_node = pyGetNode( path, true );
    }

    // input
    node = component_node.getChild("input", true);
    string input_prop = node.getString("prop");
    pos = input_prop.rfind("/");
    if ( pos != string::npos ) {
	string path = input_prop.substr(0, pos);
	input_attr = input_prop.substr(pos+1);
	printf("path = %s attr = %s\n", path.c_str(), input_attr.c_str());
	input_node = pyGetNode( path, true );
    }

    // reference
    node = component_node.getChild("reference", true);
    string ref_prop = node.getString("prop");
    ref_value = node.getString("value");
    pos = ref_prop.rfind("/");
    if ( pos != string::npos ) {
	string path = ref_prop.substr(0, pos);
	ref_attr = ref_prop.substr(pos+1);
	printf("path = %s attr = %s\n", path.c_str(), ref_attr.c_str());
	ref_node = pyGetNode( path, true );
    }

    // output
    node = component_node.getChild( "output", true );
    vector <string> children = node.getChildren();
    for ( unsigned int i = 0; i < children.size(); ++i ) {
	if ( children[i].substr(0,4) == "prop" ) {
	    string output_prop = node.getString(children[i].c_str());
	    pos = output_prop.rfind("/");
	    if ( pos != string::npos ) {
		string path = output_prop.substr(0, pos);
		string attr = output_prop.substr(pos+1);
		printf("path = %s attr = %s\n", path.c_str(), attr.c_str());
		pyPropertyNode onode = pyGetNode( path, true );
		output_node.push_back( onode );
		output_attr.push_back( attr );
	    } else {
		printf("WARNING: requested bad output path: %s\n",
		       output_prop.c_str());
	    }
	} else {
	    printf("WARNING: unknown tag in output section: %s\n",
		   children[i].c_str());
	}
    }
 
    // config
    config_node = component_node.getChild( "config", true );
    if ( config_node.hasChild("Ts") ) {
	desiredTs = config_node.getDouble("Ts");
    }
            
    if ( !config_node.hasChild("beta") ) {
	// create with default value
	config_node.setDouble( "beta", 1.0 );
    }
    if ( !config_node.hasChild("alpha") ) {
	// create with default value
	config_node.setDouble( "alpha", 0.1 );
    }
}


/*
 * Roy Vegard Ovesen:
 *
 * Ok! Here is the PID controller algorithm that I would like to see
 * implemented:
 *
 *   delta_u_n = Kp * [ (ep_n - ep_n-1) + ((Ts/Ti)*e_n)
 *               + (Td/Ts)*(edf_n - 2*edf_n-1 + edf_n-2) ]
 *
 *   u_n = u_n-1 + delta_u_n
 *
 * where:
 *
 * delta_u : The incremental output
 * Kp      : Proportional gain
 * ep      : Proportional error with reference weighing
 *           ep = beta * (r - y)
 *           where:
 *           beta : Weighing factor
 *           r    : Reference (setpoint)
 *           y    : Process value, measured
 * e       : Error
 *           e = r - y
 * Ts      : Sampling interval
 * Ti      : Integrator time
 * Td      : Derivator time
 * edf     : Derivate error with reference weighing and filtering
 *           edf_n = edf_n-1 / ((Ts/Tf) + 1) + ed_n * (Ts/Tf) / ((Ts/Tf) + 1)
 *           where:
 *           Tf : Filter time
 *           Tf = alpha * Td , where alpha usually is set to 0.1
 *           ed : Unfiltered derivate error with reference weighing
 *             ed = gamma * r - y
 *             where:
 *             gamma : Weighing factor
 * 
 * u       : absolute output
 * 
 * Index n means the n'th value.
 * 
 * 
 * Inputs:
 * enabled ,
 * y_n , r_n , beta=1 , gamma=0 , alpha=0.1 ,
 * Kp , Ti , Td , Ts (is the sampling time available?)
 * u_min , u_max
 * 
 * Output:
 * u_n
 */

void FGPIDVelComponent::update( double dt ) {
    double ep_n;            // proportional error with reference weighing
    double e_n;             // error
    double ed_n;            // derivative error
    double edf_n;           // derivative error filter
    double Tf;              // filter time
    double delta_u_n = 0.0; // incremental output
    double u_n = 0.0;       // absolute output
    double Ts;              // sampling interval (sec)
    
    elapsedTime += dt;
    if ( elapsedTime <= desiredTs ) {
        // do nothing if no time has elapsed
        return;
    }
    Ts = elapsedTime;
    elapsedTime = 0.0;

    if (!enable_node.isNull() && enable_node.getString(enable_attr.c_str()) == enable_value) {
	enabled = true;
    } else {
	enabled = false;
    }

    bool debug = component_node.getBool("debug");

    if ( Ts > 0.0) {
        if ( debug ) printf("Updating %s Ts = %.2f", get_name().c_str(), Ts );

        double y_n = 0.0;
	y_n = input_node.getDouble(input_attr.c_str());

        double r_n = 0.0;
	if ( ref_value != "" ) {
	    r_n = atof(ref_value.c_str());
	} else {
            r_n = ref_node.getDouble(ref_attr.c_str());
	}
                      
        if ( debug ) printf("  input = %.3f ref = %.3f\n", y_n, r_n );

        // Calculates proportional error:
        ep_n = config_node.getDouble("beta") * (r_n - y_n);
        if ( debug ) {
	    printf( "  ep_n = %.3f", ep_n);
	    printf( "  ep_n_1 = %.3f", ep_n_1);
	}

        // Calculates error:
        e_n = r_n - y_n;
        if ( debug ) printf( " e_n = %.3f", e_n);

        // Calculates derivate error:
        ed_n = config_node.getDouble("gamma") * r_n - y_n;
        if ( debug ) printf(" ed_n = %.3f", ed_n);

	double Td = config_node.getDouble("Td");
        if ( Td > 0.0 ) {
            // Calculates filter time:
            Tf = config_node.getDouble("alpha") * Td;
            if ( debug ) printf(" Tf = %.3f", Tf);

            // Filters the derivate error:
            edf_n = edf_n_1 / (Ts/Tf + 1)
                + ed_n * (Ts/Tf) / (Ts/Tf + 1);
            if ( debug ) printf(" edf_n = %.3f", edf_n);
        } else {
            edf_n = ed_n;
        }

        // Calculates the incremental output:
	double Ti = config_node.getDouble("Ti");
	double Kp = config_node.getDouble("Kp");
        if ( Ti > 0.0 ) {
            delta_u_n = Kp * ( (ep_n - ep_n_1)
                               + ((Ts/Ti) * e_n)
                               + ((Td/Ts) * (edf_n - 2*edf_n_1 + edf_n_2)) );
        }

        if ( debug ) {
	    printf(" delta_u_n = %.3f\n", delta_u_n);
            printf("P: %.3f  I: %.3f  D:%.3f\n",
		   Kp * (ep_n - ep_n_1),
		   Kp * ((Ts/Ti) * e_n),
		   Kp * ((Td/Ts) * (edf_n - 2*edf_n_1 + edf_n_2)));
        }

        // Integrator anti-windup logic:
	double u_min = config_node.getDouble("u_min");
	double u_max = config_node.getDouble("u_max");
        if ( delta_u_n > (u_max - u_n_1) ) {
            delta_u_n = u_max - u_n_1;
            if ( debug ) printf(" max saturation\n");
        } else if ( delta_u_n < (u_min - u_n_1) ) {
            delta_u_n = u_min - u_n_1;
            if ( debug ) printf(" min saturation\n");
        }

        // Calculates absolute output:
        u_n = u_n_1 + delta_u_n;
        if ( debug ) printf("  output = %.3f\n", u_n);

        // Updates indexed values;
        u_n_1   = u_n;
        ep_n_1  = ep_n;
        edf_n_2 = edf_n_1;
        edf_n_1 = edf_n;
    }

    if ( enabled ) {
	// Copy the result to the output node(s)
	for ( unsigned int i = 0; i < output_node.size(); i++ ) {
	    output_node[i].setDouble( output_attr[i].c_str(), u_n );
	}
    } else if ( output_node.size() > 0 ) {
	// Mirror the output value while we are not enabled so there
	// is less of a continuity break when this module is enabled

	// pull output value from the corresponding property tree value
	u_n = output_node[0].getDouble(output_attr[0].c_str());
	// and clip
	double u_min = config_node.getDouble("u_min");
	double u_max = config_node.getDouble("u_max");
 	if ( u_n < u_min ) { u_n = u_min; }
	if ( u_n > u_max ) { u_n = u_max; }
	u_n_1 = u_n;
    }
}


FGPIDComponent::FGPIDComponent( string config_path ):
    proportional( false ),
    integral( false ),
    iterm( 0.0 ),
    clamp( false ),
    y_n( 0.0 ),
    y_n_1( 0.0 ),
    r_n( 0.0 )
{
    size_t pos;

    component_node = pyGetNode(config_path, true);

    // enable
    pyPropertyNode node = component_node.getChild("enable", true);
    string enable_prop = node.getString("prop");
    enable_value = node.getString("value");
    honor_passive = node.getBool("honor_passive");
    pos = enable_prop.rfind("/");
    if ( pos != string::npos ) {
	string path = enable_prop.substr(0, pos);
	enable_attr = enable_prop.substr(pos+1);
	enable_node = pyGetNode( path, true );
    }

    // input
    node = component_node.getChild("input", true);
    string input_prop = node.getString("prop");
    pos = input_prop.rfind("/");
    if ( pos != string::npos ) {
	string path = input_prop.substr(0, pos);
	input_attr = input_prop.substr(pos+1);
	input_node = pyGetNode( path, true );
    }

    // reference
    node = component_node.getChild("reference", true);
    string ref_prop = node.getString("prop");
    ref_value = node.getString("value");
    pos = ref_prop.rfind("/");
    if ( pos != string::npos ) {
	string path = ref_prop.substr(0, pos);
	ref_attr = ref_prop.substr(pos+1);
	printf("path = %s attr = %s\n", path.c_str(), ref_attr.c_str());
	ref_node = pyGetNode( path, true );
    }


    // output
    node = component_node.getChild( "output", true );
    vector <string> children = node.getChildren();
    for ( unsigned int i = 0; i < children.size(); ++i ) {
	if ( children[i].substr(0,4) == "prop" ) {
	    string output_prop = node.getString(children[i].c_str());
	    pos = output_prop.rfind("/");
	    if ( pos != string::npos ) {
		string path = output_prop.substr(0, pos);
		string attr = output_prop.substr(pos+1);
		pyPropertyNode onode = pyGetNode( path, true );
		output_node.push_back( onode );
		output_attr.push_back( attr );
	    } else {
		printf("WARNING: requested bad output path: %s\n",
		       output_prop.c_str());
	    }
	} else {
	    printf("WARNING: unknown tag in output section: %s\n",
		   children[i].c_str());
	}
    }
 
    // config
    config_node = component_node.getChild( "config", true );
}


void FGPIDComponent::update( double dt ) {
    if (!enable_node.isNull() && enable_node.getString(enable_attr.c_str()) == enable_value) {
	enabled = true;
    } else {
	enabled = false;
    }

    bool debug = component_node.getBool("debug");
    if ( debug ) printf("Updating %s\n", get_name().c_str());
    y_n = input_node.getDouble(input_attr.c_str());

    double r_n = 0.0;
    if ( ref_value != "" ) {
	printf("nonzero ref_value\n");
	r_n = atof(ref_value.c_str());
    } else {
	r_n = ref_node.getDouble(ref_attr.c_str());
    }
                      
    double error = r_n - y_n;
    if ( debug ) printf("input = %.3f reference = %.3f error = %.3f\n",
			y_n, r_n, error);

    double u_min = config_node.getDouble("u_min");
    double u_max = config_node.getDouble("u_max");

    double Kp = config_node.getDouble("Kp");
    double Ti = config_node.getDouble("Ti");
    double Td = config_node.getDouble("Td");
    double Ki = 0.0;
    if ( Ti > 0.0001 ) {
	Ki = Kp / Ti;
    }
    double Kd = Kp * Td;

    // proportional term (and preclamp)
    double pterm = Kp * error;
    if ( pterm < u_min ) { pterm = u_min; }
    if ( pterm > u_max ) { pterm = u_max; }

    // integral term
    iterm += Ki * error * dt;
    
    // derivative term: observe that dError/dt = -dInput/dt (except
    // when the setpoint changes (which we don't want to react to
    // anyway.)  This approach avoids "derivative kick" when the set
    // point changes.
    double dy = y_n - y_n_1;
    y_n_1 = y_n;
    double dterm = Kd * -dy / dt;

    double output = pterm + iterm + dterm;
    if ( output < u_min ) {
	iterm += u_min - output;
	output = u_min;
    }
    if ( output > u_max ) {
	iterm -= output - u_max;
	output = u_max;
    }

    if ( debug ) printf("pterm = %.3f iterm = %.3f\n",
			pterm, iterm);
    if ( debug ) printf("clamped output = %.3f\n", output);

    if ( enabled ) {
	// Copy the result to the output node(s)
	for ( unsigned int i = 0; i < output_node.size(); i++ ) {
	    output_node[i].setDouble( output_attr[i].c_str(), output );
	}
    } else {
	// Force iterm to zero so we don't activate with maximum windup
	iterm = 0.0;
    }
}


FGPredictor::FGPredictor ( string config_path ):
    last_value ( 999999999.9 ),
    average ( 0.0 ),
    seconds( 0.0 ),
    filter_gain( 0.0 ),
    ivalue( 0.0 )
{
    size_t pos;

    component_node = pyGetNode(config_path);

    // enable
    pyPropertyNode node = component_node.getChild("enable", true);
    string enable_prop = node.getString("prop");
    enable_value = node.getString("value");
    honor_passive = node.getBool("honor_passive");
    pos = enable_prop.rfind("/");
    if ( pos != string::npos ) {
	string path = enable_prop.substr(0, pos);
	enable_attr = enable_prop.substr(pos+1);
	enable_node = pyGetNode( path, true );
    }

    // input
    node = component_node.getChild("input", true);
    string input_prop = node.getString("prop");
    pos = input_prop.rfind("/");
    if ( pos != string::npos ) {
	string path = input_prop.substr(0, pos);
	input_attr = input_prop.substr(pos+1);
	input_node = pyGetNode( path, true );
    }

    if ( component_node.hasChild("seconds") ) {
	seconds = component_node.getDouble("seconds");
    }
    if ( component_node.hasChild("filter_gain") ) {
	filter_gain = component_node.getDouble("filter_gain");
    }
    
    // output
    node = component_node.getChild( "output", true );
    vector <string> children = node.getChildren();
    for ( unsigned int i = 0; i < children.size(); ++i ) {
	if ( children[i].substr(0,4) == "prop" ) {
	    string output_prop = node.getString(children[i].c_str());
	    pos = output_prop.rfind("/");
	    if ( pos != string::npos ) {
		string path = output_prop.substr(0, pos);
		string attr = output_prop.substr(pos+1);
		pyPropertyNode onode = pyGetNode( path, true );
		output_node.push_back( onode );
		output_attr.push_back( attr );
	    } else {
		printf("WARNING: requested bad output path: %s\n",
		       output_prop.c_str());
	    }
	} else {
	    printf("WARNING: unknown tag in output section: %s\n",
		   children[i].c_str());
	}
    }
}

void FGPredictor::update( double dt ) {
    /*
       Simple moving average filter converts input value to predicted value "seconds".

       Smoothing as described by Curt Olson:
         gain would be valid in the range of 0 - 1.0
         1.0 would mean no filtering.
         0.0 would mean no input.
         0.5 would mean (1 part past value + 1 part current value) / 2
         0.1 would mean (9 parts past value + 1 part current value) / 10
         0.25 would mean (3 parts past value + 1 part current value) / 4

    */

    if (!enable_node.isNull() && enable_node.getString(enable_attr.c_str()) == enable_value) {
	enabled = true;
    } else {
	enabled = false;
    }

    ivalue = input_node.getDouble(input_attr.c_str());

    if ( enabled ) {
        // first time initialize average
        if (last_value >= 999999999.0) {
           last_value = ivalue;
        }

        if ( dt > 0.0 ) {
            double current = (ivalue - last_value)/dt; // calculate current error change (per second)
            if ( dt < 1.0 ) {
                average = (1.0 - dt) * average + current * dt;
            } else {
                average = current;
            }

            // calculate output with filter gain adjustment
            double output = ivalue + (1.0 - filter_gain) * (average * seconds) + filter_gain * (current * seconds);

	    // Copy the result to the output node(s)
	    for ( unsigned int i = 0; i < output_node.size(); i++ ) {
		output_node[i].setDouble( output_attr[i].c_str(), output );
	    }
        }
        last_value = ivalue;
    }
}


FGDigitalFilter::FGDigitalFilter( string config_path )
{
    size_t pos;
    samples = 1;

    component_node = pyGetNode(config_path, true);

    // enable
    pyPropertyNode node = component_node.getChild("enable", true);
    string enable_prop = node.getString("prop");
    enable_value = node.getString("value");
    honor_passive = node.getBool("honor_passive");
    pos = enable_prop.rfind("/");
    if ( pos != string::npos ) {
	string path = enable_prop.substr(0, pos);
	enable_attr = enable_prop.substr(pos+1);
	enable_node = pyGetNode( path, true );
    }

    // input
    node = component_node.getChild("input", true);
    string input_prop = node.getString("prop");
    pos = input_prop.rfind("/");
    if ( pos != string::npos ) {
	string path = input_prop.substr(0, pos);
	input_attr = input_prop.substr(pos+1);
	input_node = pyGetNode( path, true );
    }

    if ( component_node.hasChild("type") ) {
	string cval = component_node.getString("type");
	if ( cval == "exponential" ) {
	    filterType = exponential;
	} else if (cval == "double-exponential") {
	    filterType = doubleExponential;
	} else if (cval == "moving-average") {
	    filterType = movingAverage;
	} else if (cval == "noise-spike") {
	    filterType = noiseSpike;
	}
    }
    if ( component_node.hasChild("filter_time") ) {
	Tf = component_node.getDouble("filter_time");
    }
    if ( component_node.hasChild("samples") ) {
	samples = component_node.getLong("samples");
    }
    if ( component_node.hasChild("max_rate_of_change") ) {
	rateOfChange = component_node.getDouble("max_rate_of_change");
    }

    // output
    node = component_node.getChild( "output", true );
    vector <string> children = node.getChildren();
    for ( unsigned int i = 0; i < children.size(); ++i ) {
	if ( children[i].substr(0,4) == "prop" ) {
	    string output_prop = node.getString(children[i].c_str());
	    pos = output_prop.rfind("/");
	    if ( pos != string::npos ) {
		string path = output_prop.substr(0, pos);
		string attr = output_prop.substr(pos+1);
		pyPropertyNode onode = pyGetNode( path, true );
		output_node.push_back( onode );
		output_attr.push_back( attr );
	    } else {
		printf("WARNING: requested bad output path: %s\n",
		       output_prop.c_str());
	    }
	} else {
	    printf("WARNING: unknown tag in output section: %s\n",
		   children[i].c_str());
	}
    }

    output.resize(2, 0.0);
    input.resize(samples + 1, 0.0);
}

void FGDigitalFilter::update(double dt)
{
    if (!enable_node.isNull() && enable_node.getString(enable_attr.c_str()) == enable_value) {
	enabled = true;
    } else {
	enabled = false;
    }

    input.push_front( input_node.getDouble(input_attr.c_str()) );
    input.resize(samples + 1, 0.0);

    if ( enabled && dt > 0.0 ) {
        /*
         * Exponential filter
         *
         * Output[n] = alpha*Input[n] + (1-alpha)*Output[n-1]
         *
         */

        if (filterType == exponential)
        {
            double alpha = 1 / ((Tf/dt) + 1);
            output.push_front(alpha * input[0] + 
                              (1 - alpha) * output[0]);
	    for ( unsigned int i = 0; i < output_node.size(); i++ ) {
		output_node[i].setDouble( output_attr[i].c_str(), output[0] );
	    }
            output.resize(1);
        } 
        else if (filterType == doubleExponential)
        {
            double alpha = 1 / ((Tf/dt) + 1);
            output.push_front(alpha * alpha * input[0] + 
                              2 * (1 - alpha) * output[0] -
                              (1 - alpha) * (1 - alpha) * output[1]);
 	    for ( unsigned int i = 0; i < output_node.size(); i++ ) {
		output_node[i].setDouble( output_attr[i].c_str(), output[0] );
	    }
            output.resize(2);
        }
        else if (filterType == movingAverage)
        {
            output.push_front(output[0] + 
                              (input[0] - input.back()) / samples);
 	    for ( unsigned int i = 0; i < output_node.size(); i++ ) {
		output_node[i].setDouble( output_attr[i].c_str(), output[0] );
	    }
            output.resize(1);
        }
        else if (filterType == noiseSpike)
        {
            double maxChange = rateOfChange * dt;

            if ((output[0] - input[0]) > maxChange)
            {
                output.push_front(output[0] - maxChange);
            }
            else if ((output[0] - input[0]) < -maxChange)
            {
                output.push_front(output[0] + maxChange);
            }
            else if (fabs(input[0] - output[0]) <= maxChange)
            {
                output.push_front(input[0]);
            }

 	    for ( unsigned int i = 0; i < output_node.size(); i++ ) {
		output_node[i].setDouble( output_attr[i].c_str(), output[0] );
	    }
	    output.resize(1);
        }
        if ( component_node.getBool("debug") ) {
            printf("input: %.3f\toutput: %.3f\n", input[0], output[0]);
        }
    }
}


FGXMLAutopilot::FGXMLAutopilot() {
}


FGXMLAutopilot::~FGXMLAutopilot() {
}

 
void FGXMLAutopilot::init() {
    if ( ! build() ) {
	printf("Detected an internal inconsistency in the autopilot\n");
	printf("configuration.  See earlier errors for details.\n" );
	exit(-1);
    }
    log_ap_config();
}


void FGXMLAutopilot::reinit() {
    components.clear();
    init();
    build();
}


void FGXMLAutopilot::bind() {
}

void FGXMLAutopilot::unbind() {
}

bool FGXMLAutopilot::build() {
    pyPropertyNode config_props = pyGetNode( "/config/autopilot", true );

    // FIXME: we have always depended on the order of children
    // components here to ensure PID stages are run in the correct
    // order, however that is a bad thing to assume ... especially now
    // with pyprops!!!
    vector <string> children = config_props.getChildren();
    for ( unsigned int i = 0; i < children.size(); ++i ) {
	pyPropertyNode component = config_props.getChild(children[i].c_str(),
							 true);
        printf("ap stage: %s\n", children[i].c_str());
	string name = children[i];
	size_t pos = name.find("[");
	if ( pos != string::npos ) {
	    name = name.substr(0, pos);
	}
	if ( name == "component" ) {
	    ostringstream config_path;
	    config_path << "/config/autopilot/" << children[i];
	    string module = component.getString("module");
	    if ( module == "pid_vel_component" ) {
		FGXMLAutoComponent *c
		    = new FGPIDVelComponent( config_path.str() );
		components.push_back( c );
	    } else if ( module == "pid_component" ) {
		FGXMLAutoComponent *c
		    = new FGPIDComponent( config_path.str() );
		components.push_back( c );
	    } else if ( module == "predict_simple" ) {
		FGXMLAutoComponent *c
		    = new FGPredictor( config_path.str() );
		components.push_back( c );
	    } else if ( module == "filter" ) {
		FGXMLAutoComponent *c
		    = new FGDigitalFilter( config_path.str() );
		components.push_back( c );
	    } else {
		printf("Unknown AP module name: %s\n", module.c_str());
		return false;
	    }
	} else if ( name == "L1_controller" ) {
	    // information placeholder, we don't do anything here.
        } else {
	    printf("Unknown top level section: %s\n", children[i].c_str() );
            return false;
        }
    }

    return true;
}


// normalize a value to lie between min and max
template <class T>
inline void SG_NORMALIZE_RANGE( T &val, const T min, const T max ) {
    T step = max - min;
    while( val >= max )  val -= step;
    while( val < min ) val += step;
};


/*
 * Update the list of autopilot components
 */

void FGXMLAutopilot::update( double dt ) {
    for ( unsigned int i = 0; i < components.size(); ++i ) {
        components[i]->update( dt );
    }
}
