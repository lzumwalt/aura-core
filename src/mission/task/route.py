import math

from props import root, getNode

import comms.events
import control.route

from task import Task

class Route(Task):
    def __init__(self, config_node):
        Task.__init__(self)
        self.route_node = getNode('/task/route', True)
        self.ap_node = getNode('/autopilot', True)
        self.nav_node = getNode("/navigation", True)
        self.targets_node = getNode('/autopilot/targets', True)

        self.alt_agl_ft = 0.0
        self.speed_kt = 30.0

        self.saved_fcs_mode = ''
        self.saved_nav_mode = ''
        self.saved_agl_ft = 0.0
        self.saved_speed_kt = 0.0

        self.name = config_node.getString('name')
        self.nickname = config_node.getString('nickname')
        self.coord_path = config_node.getString('coord_path')
        self.alt_agl_ft = config_node.getFloat('altitude_agl_ft')
        self.speed_kt = config_node.getFloat('speed_kt')

        # load a route if included in config tree
        if control.route.build(config_node):
            control.route.swap()
        else:
            print 'Detected an internal inconsistency in the route'
	    print ' configuration.  See earlier errors for details.'
	    quit()
            
    def activate(self):
        self.active = True

        # save existing state
        self.saved_fcs_mode = self.ap_node.getString('mode')
        self.saved_nav_mode = self.nav_node.getString('mode')
        self.saved_agl_ft = self.targets_node.getFloat('altitude_agl_ft')
        self.saved_speed_kt = self.targets_node.getFloat('airspeed_kt')

        # set modes
        self.ap_node.setString('mode', 'basic+alt+speed')
        self.nav_node.setString('mode', 'route')

        if self.alt_agl_ft > 0.1:
            self.targets_node.setFloat('altitude_agl_ft', self.alt_agl_ft)

        self.route_node.setString('follow_mode', 'leader');
        self.route_node.setString('start_mode', 'first_wpt');
        self.route_node.setString('completion_mode', 'loop');
        
        comms.events.log('mission', 'route')

    # build route from a property tree node
    def build(self, config_node):
        self.standby_route = []       # clear standby route
        for child_name in config_node.getChildren():
            if child_name == 'name':
                # ignore this for now
                pass                
            elif child_name[:3] == 'wpt':
                child = config_node.getChild(child_name)
                wp = waypoint.Waypoint()
                wp.build(child)
                self.standby_route.append(wp)
            elif child_name == 'enable':
                # we do nothing on this tag right now, fixme: remove
                # this tag from all routes?
                pass
            else:
                print 'Unknown top level section:', child_name
                return False
        print 'loaded %d waypoints' % len(self.standby_route)
        return True

    def update(self, dt):
        if not self.active:
            return False

    def is_complete(self):
        return False
    
    def close(self):
        # restore the previous state
        self.ap_node.setString('mode', self.saved_fcs_mode)
        self.nav_node.setString('mode', self.saved_nav_mode)
        self.targets_node.setFloat('altitude_agl_ft', self.saved_agl_ft)
        self.targets_node.setFloat('airspeed_kt', self.saved_speed_kt)

        self.active = False
        return True
