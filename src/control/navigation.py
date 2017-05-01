# high level navigation modes

from props import root, getNode

import circle
import route

nav_node = getNode("/navigation", True)

def init():
    circle.init()
    route.init()

def update(dt):
    if nav_node.getString("mode") == 'circle':
        circle.update(dt)
    elif nav_node.getString("mode") == 'route':
        route.update(dt)
