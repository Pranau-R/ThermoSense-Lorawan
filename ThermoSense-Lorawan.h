/*

Name:   ThermoSense-Lorawan.h

Function:
    Global linkage for ThermoSense-Lorawan.ino

Copyright:
    See accompanying LICENSE file for copyright and license information.

Author:
    Dhinesh Kumar Pitchai, MCCI Corporation   May 2022

*/

#ifndef _ThermoSense-Lorawan_h_
# define _ThermoSense-Lorawan_h_

#pragma once

#include <Catena.h>
#include <Catena_Led.h>
#include <Catena_Mx25v8035f.h>
#include <Catena_Timer.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <SPI.h>
#include "Catena4610_cMeasurementLoop.h"

// the global clock object

extern  McciCatena::Catena                      gCatena;
extern  McciCatena::cTimer                      ledTimer;
extern  McciCatena::Catena::LoRaWAN             gLoRaWAN;
extern  McciCatena::StatusLed                   gLed;

extern  SPIClass                                gSPI2;
extern  McciCatena4610::cMeasurementLoop        gMeasurementLoop;

//   The Temp Probe
extern  OneWire                                 oneWire;
extern  DallasTemperature                       sensor_CompostTemp;
extern bool                                     fHasCompostTemp;

//   The flash
extern  McciCatena::Catena_Mx25v8035f           gFlash;

#endif // !defined(_ThermoSense-Lorawan_h_)
