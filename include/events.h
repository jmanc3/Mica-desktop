#pragma once

#include <vector>

struct Container;

struct Event {
    float x;
    float y;

    int button;
    int state;

    bool scroll = false;
    int axis = 0;
    int direction = 0;
    double delta = 0.0;
    int descrete = 0;
    bool from_mouse = false;
 

    Event(float x, float y, int button, int state) : x(x), y(y), button(button), state(state) {
       ; 
    }
    
    Event(float x, float y) : x(x), y(y) {
       ; 
    }

    Event () { 
        ;
    }
};

void mouse_entered(Container*, const Event&);
void mouse_left(Container*, const Event&);
void move_event(Container*, const Event&);
void mouse_event(Container*, const Event&);

void paint_root(Container*);
void paint_outline(Container*, Container*);

std::vector<Container*> pierced_containers(Container* root, int x, int y);

