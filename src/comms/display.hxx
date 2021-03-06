#ifndef _AURA_DISPLAY_HXX
#define _AURA_DISPLAY_HXX

// import a python module and call it's init() and update() routines
// requires imported python modules to follow some basic rules to play
// nice.  (see examples in the code for now.)

#include "python/pymodule.hxx"

extern bool display_on;

class pyModuleDisplay: public pyModuleBase {

public:

    // constructor / destructor
    pyModuleDisplay();
    ~pyModuleDisplay() {}

    bool show(const char *message);
    void status_summary();
};

#endif // _AURA_DISPLAY_HXX
