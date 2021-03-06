/**
 * \file: filter_mgr.cpp
 *
 * Front end management interface for executing the available filter codes.
 *
 * Copyright (C) 2009 - Curtis L. Olson curtolson <at> gmail <dot> com
 *
 * $Id: adns_mgr.cpp,v 1.6 2009/05/15 17:04:56 curt Exp $
 */


#include "python/pyprops.hxx"

#include <stdio.h>
#include <string>
#include <sstream>
using std::string;
using std::ostringstream;

#include "comms/remote_link.hxx"
#include "comms/logging.hxx"
#include "filters/nav_eigen/aura_interface.hxx"
#include "filters/nav_eigen_mag/aura_interface.hxx"
#include "filters/umngnss_quat/umngnss_quat.hxx"
#include "include/globaldefs.h"
#include "init/globals.hxx"
#include "util/lowpass.hxx"
#include "util/myprof.hxx"

#include "filter_mgr.hxx"

//
// Global variables
//

static double last_imu_time = 0.0;

// property nodes
static pyPropertyNode imu_node;
static pyPropertyNode gps_node;
static pyPropertyNode pos_node;
static pyPropertyNode orient_node;
static pyPropertyNode vel_node;
static pyPropertyNode pos_filter_node;
static pyPropertyNode pos_pressure_node;
static pyPropertyNode pos_combined_node;
static pyPropertyNode filter_node;
static pyPropertyNode filter_group_node;
static pyPropertyNode task_node;
static pyPropertyNode airdata_node;
static pyPropertyNode wind_node;
static pyPropertyNode status_node;
static vector<pyPropertyNode> sections;

// initial values are the 'time factor'
static LowPassFilter ground_alt_filt( 30.0 );
static LowPassFilter we_filt( 60.0 );
static LowPassFilter wn_filt( 60.0 );
static LowPassFilter pitot_scale_filt( 30.0 );

static bool ground_alt_calibrated = false;

static int remote_link_skip = 0;
static int logging_skip = 0;

void Filter_init() {
    // initialize imu property nodes
    imu_node = pyGetNode("/sensors/imu", true);
    gps_node = pyGetNode("/sensors/gps", true);
    pos_node = pyGetNode("/position", true);
    orient_node = pyGetNode("/orientation", true);
    vel_node = pyGetNode("/velocity", true);
    filter_node = pyGetNode("/filters/filter", true);
    filter_group_node = pyGetNode("/filters", true);
    pos_filter_node = pyGetNode("/position/filter", true);
    pos_pressure_node = pyGetNode("/position/pressure", true);
    pos_combined_node = pyGetNode("/position/combined", true);
    task_node = pyGetNode("/task", true);
    airdata_node = pyGetNode("/sensors/airdata", true);
    wind_node = pyGetNode("/filters/wind", true);
    status_node = pyGetNode("/status", true);

    wind_node.setDouble( "pitot_scale_factor", 1.0 );
    
    pyPropertyNode remote_link_node = pyGetNode("/config/remote_link", true);
    pyPropertyNode logging_node = pyGetNode("/config/logging", true);
    remote_link_skip = remote_link_node.getLong("filter_skip");
    logging_skip = logging_node.getLong("filter_skip");

    // traverse configured modules
    pyPropertyNode group_node = pyGetNode("/config/filters", true);
    vector<string> children = group_node.getChildren();
    printf("Found %d filter sections\n", (int)children.size());
    for ( unsigned int i = 0; i < children.size(); i++ ) {
	pyPropertyNode section = group_node.getChild(children[i].c_str());
	sections.push_back(section);
	string module = section.getString("module");
	bool enabled = section.getBool("enable");
	if ( !enabled ) {
	    continue;
	}
	ostringstream output_path;
	output_path << "/filters/filter" << '[' << i << ']';
	printf("filter: %d = %s\n", i, module.c_str());
	if ( module == "null" ) {
	    // do nothing
	} else if ( module == "nav_eigen" ) {
	    nav_eigen_init( output_path.str(), &section );
	} else if ( module == "nav_eigen_mag" ) {
	    nav_eigen_mag_init( output_path.str(), &section );
	} else if ( module == "umn_quat" ) {
	    umngnss_quat_init( output_path.str(), &section );
	} else {
	    printf("Unknown filter = '%s' in config file\n",
		   module.c_str());
	}
    }

    pitot_scale_filt.init(1.0);
}


static void update_euler_rates() {
    double phi = orient_node.getDouble("roll_deg") * SGD_DEGREES_TO_RADIANS;
    double the = orient_node.getDouble("pitch_deg") * SGD_DEGREES_TO_RADIANS;

    // direct computation of euler rates given body rates and estimated
    // attitude (based on googled references):
    // http://www.princeton.edu/~stengel/MAE331Lecture9.pdf
    // http://www.mathworks.com/help/aeroblks/customvariablemass6dofeulerangles.html

    double p = imu_node.getDouble("p_rad_sec");
    double q = imu_node.getDouble("q_rad_sec");
    double r = imu_node.getDouble("r_rad_sec");

    if ( SGD_PI_2 - fabs(the) > 0.00001 ) {
	double phi_dot = p + q * sin(phi) * tan(the) + r * cos(phi) * tan(the);
	double the_dot = q * cos(phi) - r * sin(phi);
	double psi_dot = q * sin(phi) / cos(the) + r * cos(phi) / cos(the);
	orient_node.setDouble("phi_dot_rad_sec", phi_dot);
	orient_node.setDouble("the_dot_rad_sec", the_dot);
	orient_node.setDouble("psi_dot_rad_sec", psi_dot);
	/* printf("dt=%.3f q=%.3f q(ned)=%.3f phi(dot)=%.3f\n",
	   dt,imu_node.getDouble("q_rad_sec"), dq/dt, phi_dot);  */
	/* printf("%.3f %.3f %.3f %.3f\n",
	   cur_time,imu_node.getDouble("q_rad_sec"), dq/dt, the_dot); */
   }
}


static void update_ground(double dt) {
    // determine ground reference altitude.  Average filter altitude
    // over the most recent 30 seconds that we are !is_airborne
    if ( !ground_alt_calibrated ) {
	ground_alt_calibrated = true;
	ground_alt_filt.init( filter_node.getDouble("altitude_m") );
    }

    if ( ! task_node.getBool("is_airborne") ) {
	// ground reference altitude averaged current altitude over
	// first 30 seconds while on the ground
	ground_alt_filt.update( filter_node.getDouble("altitude_m"), dt );
	pos_filter_node.setDouble( "altitude_ground_m",
				   ground_alt_filt.get_value() );
    }

    float agl_m = filter_node.getDouble( "altitude_m" )
	- ground_alt_filt.get_value();
    pos_filter_node.setDouble( "altitude_agl_m", agl_m );
    pos_filter_node.setDouble( "altitude_agl_ft", agl_m * SG_METER_TO_FEET );
}


// onboard wind estimate (requires airspeed, true heading, and ground
// velocity vector)
static void update_wind(double dt) {
    // Estimate wind direction and speed based on ground track speed
    // versus aircraft heading and indicated airspeed.

    double airspeed_kt = airdata_node.getDouble("airspeed_kt");
    if ( ! task_node.getBool("is_airborne") ) {
	// System predicts we are not flying.  The wind estimation
	// code only works in flight.
	return;
    }

    double psi = SGD_PI_2
	- orient_node.getDouble("heading_deg") * SG_DEGREES_TO_RADIANS;
    double pitot_scale = pitot_scale_filt.get_value();
    double ue = cos(psi) * (airspeed_kt * pitot_scale * SG_KT_TO_MPS);
    double un = sin(psi) * (airspeed_kt * pitot_scale * SG_KT_TO_MPS);
    double we = ue - filter_node.getDouble("ve_ms");
    double wn = un - filter_node.getDouble("vn_ms");

    //static double filt_we = 0.0, filt_wn = 0.0;
    //filt_we = 0.9998 * filt_we + 0.0002 * we;
    //filt_wn = 0.9998 * filt_wn + 0.0002 * wn;
    we_filt.update(we, dt);
    wn_filt.update(wn, dt);

    double we_filt_val = we_filt.get_value();
    double wn_filt_val = wn_filt.get_value();
    
    double wind_deg = 90
	- atan2( wn_filt_val, we_filt_val ) * SGD_RADIANS_TO_DEGREES;
    if ( wind_deg < 0 ) { wind_deg += 360.0; }
    double wind_speed_kt = sqrt( we_filt_val*we_filt_val
				 + wn_filt_val*wn_filt_val ) * SG_MPS_TO_KT;

    wind_node.setDouble( "wind_speed_kt", wind_speed_kt );
    wind_node.setDouble( "wind_dir_deg", wind_deg );
    wind_node.setDouble( "wind_east_mps", we_filt_val );
    wind_node.setDouble( "wind_north_mps", wn_filt_val );

    // estimate pitot tube bias
    double true_e = we_filt_val + filter_node.getDouble("ve_ms");
    double true_n = wn_filt_val + filter_node.getDouble("vn_ms");

    double true_deg = 90 - atan2( true_n, true_e ) * SGD_RADIANS_TO_DEGREES;
    if ( true_deg < 0 ) { true_deg += 360.0; }
    double true_speed_kt = sqrt( true_e*true_e + true_n*true_n ) * SG_MPS_TO_KT;

    wind_node.setDouble( "true_airspeed_kt", true_speed_kt );
    wind_node.setDouble( "true_heading_deg", true_deg );
    wind_node.setDouble( "true_airspeed_east_mps", true_e );
    wind_node.setDouble( "true_airspeed_north_mps", true_n );

    double ps = 1.0;
    if ( airspeed_kt > 1.0 ) {
	ps = true_speed_kt / airspeed_kt;
	// don't let the scale factor exceed some reasonable limits
	if ( ps < 0.75 ) { ps = 0.75;	}
	if ( ps > 1.25 ) { ps = 1.25; }
    }

    pitot_scale_filt.update(ps, dt);
    wind_node.setDouble( "pitot_scale_factor", pitot_scale_filt.get_value() );

    // if ( display_on ) {
    //   printf("true: %.2f kt  %.1f deg (scale = %.4f)\n", true_speed_kt, true_deg, pitot_scale_filt);
    // }

    // now estimate ground speed/track based on airdata and wind estimate
    double ve_est = ue - we_filt_val;
    double vn_est = un - wn_filt_val;
    double groundtrack_est_deg = 90 - atan2( vn_est, ve_est ) * SGD_RADIANS_TO_DEGREES;
    if ( groundtrack_est_deg < 0 ) { groundtrack_est_deg += 360.0; }
    double groundspeed_est_ms = sqrt( ve_est*ve_est + vn_est*vn_est );
    double groundspeed_est_kt = groundspeed_est_ms * SG_MPS_TO_KT;
    vel_node.setDouble( "groundspeed_est_ms", groundspeed_est_ms );
    orient_node.setDouble( "groundtrack_est_deg", groundtrack_est_deg );
}


static void publish_values() {
    orient_node.setDouble( "roll_deg", filter_node.getDouble("roll_deg") );
    orient_node.setDouble( "pitch_deg", filter_node.getDouble("pitch_deg") );
    orient_node.setDouble( "heading_deg", filter_node.getDouble("heading_deg") );
    pos_node.setDouble( "latitude_deg", filter_node.getDouble("latitude_deg") );
    pos_node.setDouble( "longitude_deg", filter_node.getDouble("longitude_deg") );
    pos_filter_node.setDouble("altitude_m", filter_node.getDouble("altitude_m"));
    pos_filter_node.setDouble("altitude_ft", filter_node.getDouble("altitude_ft"));
    vel_node.setDouble( "vn_ms", filter_node.getDouble("vn_ms") );
    vel_node.setDouble( "ve_ms", filter_node.getDouble("ve_ms") );
    vel_node.setDouble( "vd_ms", filter_node.getDouble("vd_ms") );
    filter_group_node.setDouble( "timestamp",
				 filter_node.getDouble("timestamp") );
    status_node.setString( "navigation",
			   filter_node.getString("navigation") );
    bool use_filter = true;
    bool use_gps = !use_filter;
    if ( use_filter ) {
	orient_node.setDouble( "groundtrack_deg",
			       filter_node.getDouble("groundtrack_deg") );
	vel_node.setDouble( "groundspeed_ms",
			    filter_node.getDouble("groundspeed_ms") );
    } else if ( use_gps ) {
	const double R2D = 57.295779513082323;
	double vn = gps_node.getDouble("vn_ms");
	double ve = gps_node.getDouble("ve_ms");
	filter_node.setDouble( "groundtrack_deg", 90 - atan2(vn, ve) * R2D );
	filter_node.setDouble( "groundspeed_ms", sqrt(vn*vn + ve*ve) );
    }
   
    vel_node.setDouble( "vertical_speed_fps",
			filter_node.getDouble("vertical_speed_fps") );
    
    // select official source (currently AGL is pressure based,
    // absolute ground alt is based on average gps/filter value at
    // startup, and MSL altitude is based on pressure altitude -
    // pressure error (pressure error computed as average difference
    // between gps altitude and pressure altitude over time)):
    //
    // 1. /position/pressure
    // 2. /position/filter
    // 3. /position/combined
    //official_alt_m_node->alias("/position/combined/altitude-true-m");
    //official_alt_ft_node->alias("/position/combined/altitude-true-ft");
    //official_agl_m_node->alias("/position/pressure/altitude-agl-m");
    //official_agl_ft_node->alias("/position/pressure/altitude-agl-ft");
    //official_ground_m_node->alias("/position/filter/altitude-ground-m");    

    // the following block favors the baro based altimeter, but can
    // suffer from cabin pressure change bias, temperature bias, or
    // other unexplained biases.
    // pos_node.setDouble( "altitude_m",
    //     		pos_combined_node.getDouble("altitude_true_m") );
    // pos_node.setDouble( "altitude_ft",
    //     		pos_combined_node.getDouble("altitude_true_ft") );
    // pos_node.setDouble( "altitude_agl_m",
    //     		pos_pressure_node.getDouble("altitude_agl_m") );
    // pos_node.setDouble( "altitude_agl_ft",
    //     		pos_pressure_node.getDouble("altitude_agl_ft") );
    // pos_node.setDouble( "altitude_ground_m",
    //     		pos_filter_node.getDouble("altitude_ground_m") );

    // the following block favor the filter based altitude which can
    // be adversely affected (significantly) by gps altitude errors.
    pos_node.setDouble( "altitude_m",
			pos_filter_node.getDouble("altitude_m") );
    pos_node.setDouble( "altitude_ft",
			pos_filter_node.getDouble("altitude_ft") );
    pos_node.setDouble( "altitude_agl_m",
			pos_filter_node.getDouble("altitude_agl_m") );
    pos_node.setDouble( "altitude_agl_ft",
			pos_filter_node.getDouble("altitude_agl_ft") );
    pos_node.setDouble( "altitude_ground_m",
			pos_filter_node.getDouble("altitude_ground_m") );
}

bool Filter_update() {
    filter_prof.start();

    double imu_time = imu_node.getDouble("timestamp");
    double imu_dt = imu_time - last_imu_time;
    bool fresh_filter_data = false;

    // sanity check (i.e. if system clock was changed by another process)
    if ( imu_dt > 1.0 ) { imu_dt = 0.01; }
    if ( imu_dt < 0.0 ) { imu_dt = 0.01; }

    static int remote_link_count = 0;
    static int logging_count = 0;

    // traverse configured modules
    for ( unsigned int i = 0; i < sections.size(); i++ ) {
	string module = sections[i].getString("module");
	bool enabled = sections[i].getBool("enable");
	if ( !enabled ) {
	    continue;
	}
	if ( module == "null" ) {
	    // do nothing
	} else if ( module == "nav_eigen" ) {
	    fresh_filter_data = nav_eigen_update();
	} else if ( module == "nav_eigen_mag" ) {
	    fresh_filter_data = nav_eigen_mag_update();
	} else if ( module == "umn_quat" ) {
	    fresh_filter_data = umngnss_quat_update();
	}
	if ( fresh_filter_data ) {
	    if ( i == 0 ) {
		// only for primary filter
		update_euler_rates();
		update_ground(imu_dt);
		update_wind(imu_dt);
		publish_values();
	    }
	}

	bool send_remote_link = false;
	if ( remote_link_count < 0 ) {
	    send_remote_link = true;
	    remote_link_count = remote_link_skip;
	}
	
	bool send_logging = false;
	if ( logging_count < 0 ) {
	    send_logging = true;
	    logging_count = logging_skip;
	}
	
	if ( send_remote_link || send_logging ) {
	    uint8_t buf[256];
	    int size = packer->pack_filter( i, buf );
	    if ( send_remote_link ) {
		remote_link->send_message( buf, size );
	    }
	    if ( send_logging ) {
		logging->log_message( buf, size );
	    }
	}
    }

    filter_prof.stop();

    if ( fresh_filter_data ) {
        remote_link_count--;
        logging_count--;
    }
	     
    last_imu_time = imu_time;

#if 0
    //static SGPropertyNode *tp = fgGetNode("/sensors/imu/pitch-truth-deg", true);
    //static SGPropertyNode *ep = fgGetNode("/orientation/pitch-deg", true);
    printf("pitch error: %.2f (true = %.2f est = %.2f)\n", ep.getDouble() - tp.getDouble(), tp.getDouble(), ep.getDouble());
#endif

    return fresh_filter_data;
}


void Filter_close() {
    // traverse configured modules
    for ( unsigned int i = 0; i < sections.size(); i++ ) {
	string module = sections[i].getString("module");
	bool enabled = sections[i].getBool("enable");
	if ( !enabled ) {
	    continue;
	}
	if ( module == "null" ) {
	    // do nothing
	} else if ( module == "nav_eigen" ) {
	    nav_eigen_close();
	} else if ( module == "nav_eigen_mag" ) {
	    nav_eigen_mag_close();
	} else if ( module == "umn_quat" ) {
	    umngnss_quat_close();
	}
    }
}
