//
// FILE: gps_fgfs.hxx
// DESCRIPTION: aquire live sensor data from a live running instance
// of Flightgear
//

#ifndef _AURA_GPS_FGFS_HXX
#define _AURA_GPS_FGFS_HXX


#include "include/globaldefs.h"

#include "props/props.hxx"


// function prototypes
bool fgfs_gps_init( string rootname, SGPropertyNode *config );
bool fgfs_gps_update();
void fgfs_gps_close();

bool fgfs_gps_get();


#endif // _AURA_GPS_FGFS_HXX
