//
// FILE: Aura3.hxx
// DESCRIPTION: interact with Aura3 (Teensy/Pika) sensor head
//

#ifndef _AURA_AURA3_SENSORS_HXX
#define _AURA_AURA3_SENSORS_HXX


#include "python/pyprops.hxx"

#include "include/globaldefs.h"


// function prototypes

double Aura3_update();
void Aura3_close();
bool Aura3_request_baud( uint32_t baud );

bool Aura3_imu_init( string output_path, pyPropertyNode *config );
bool Aura3_imu_update();
void Aura3_imu_close();

bool Aura3_gps_init( string output_path, pyPropertyNode *config  );
bool Aura3_gps_update();
void Aura3_gps_close();

bool Aura3_airdata_init( string output_path );
bool Aura3_airdata_update();
// force an airspeed zero calibration (ideally with the aircraft on
// the ground with the pitot tube perpendicular to the prevailing
// wind.)
void Aura3_airdata_zero_airspeed();
void Aura3_airdata_close();

bool Aura3_pilot_init( string output_path, pyPropertyNode *config );
bool Aura3_pilot_update();
void Aura3_pilot_close();

bool Aura3_act_init( pyPropertyNode *config );
bool Aura3_act_update();
void Aura3_act_close();
extern bool Aura3_actuator_configured;

#endif // _AURA_AURA3_SENSORS_HXX
