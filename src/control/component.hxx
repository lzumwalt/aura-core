// component.hxx - autopilot component base class
//
// Written by Curtis Olson, started January 2004.
//
// Copyright (C) 2004-2017  Curtis L. Olson  - curtolson@flightgear.org
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

#ifndef _AURA_AP_COMPONENT_HXX
#define _AURA_AP_COMPONENT_HXX

#ifndef __cplusplus
# error This library requires C++
#endif

#include "python/pyprops.hxx"

#include <string>
#include <vector>

using std::vector;
using std::string;

/**
 * Base class for other autopilot components
 */

class APComponent {

protected:

    pyPropertyNode component_node;
    
    pyPropertyNode enable_node;
    string enable_attr;
    string enable_value;
    bool honor_passive;
    bool enabled;

    pyPropertyNode input_node;
    string input_attr;
    
    pyPropertyNode ref_node;
    string ref_attr;
    string ref_value;
  
    string r_n_prop;
    string r_n_value;

    vector <pyPropertyNode> output_node;
    vector <string> output_attr;

    pyPropertyNode config_node;

public:

    APComponent() :
      enable_value( "" ),
      honor_passive( false ),
      enabled( false )
    { }

    virtual ~APComponent() {}

    virtual void update (double dt)=0;
    
    inline string get_name() { return component_node.getString("name"); }
};


#endif // _AURA_AP_COMPONENT_HXX
