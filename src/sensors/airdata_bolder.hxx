/**
 * \file: airdata_bolder.hxx
 *
 * Driver for the Bolder Flight Systems airdata module (build on AMSYS pressure
 * sensors.)
 *
 * Copyright (C) 2016 - Curtis L. Olson - curtolson@flightgear.org
 *
 */

#ifndef _AURA_AIRDATA_BOLDER_HXX
#define _AURA_AIRDATA_BOLDER_HXX


#include "python/pyprops.hxx"

#include <string>
using std::string;

#include "include/globaldefs.h"


void airdata_bolder_init( string output_path, pyPropertyNode *config );
bool airdata_bolder_update();
void airdata_bolder_zero_airspeed();
void airdata_bolder_close();


#endif // _AURA_AIRDATA_BOLDER_HXX
