/*

Module: Catena4610_cMeasurementLoop_fillBuffer.cpp

Function:
    Class for transmitting accumulated measurements.

Copyright:
    See accompanying LICENSE file for copyright and license information.

Author:
    Dhinesh Kumar Pitchai, MCCI Corporation   May 2022

*/

#include <Catena_TxBuffer.h>

#include "Catena4610_cMeasurementLoop.h"

#include <arduino_lmic.h>

using namespace McciCatena;
using namespace McciCatena4610;

/*

Name:   McciCatena4610::cMeasurementLoop::fillTxBuffer()

Function:
    Prepare a messages in a TxBuffer with data from current measurements.

Definition:
    void McciCatena4610::cMeasurementLoop::fillTxBuffer(
            cMeasurementLoop::TxBuffer_t& b
            );

Description:
    A format 0x22 message is prepared from the data in the cMeasurementLoop
    object.

*/

void
cMeasurementLoop::fillTxBuffer(
    cMeasurementLoop::TxBuffer_t& b, Measurement const &mData
    )
    {
    gLed.Set(McciCatena::LedPattern::Measuring);


    // initialize the message buffer to an empty state
    b.begin();

    // insert format byte
    b.put(kMessageFormat);

    // the flags in Measurement correspond to the over-the-air flags.
    b.put(std::uint8_t(this->m_data.flags));

    // send Vbat
    if ((this->m_data.flags &  Flags::FlagVbat) !=  Flags(0))
        {
        float Vbat = mData.Vbat;
        gCatena.SafePrintf("Vbat:    %d mV\n", (int) (Vbat * 1000.0f));
        b.putV(Vbat);
        }

    // send Vdd if we can measure it.

    // Vbus is sent as 5000 * v
    if ((this->m_data.flags &  Flags::FlagVcc) !=  Flags(0))
        {
        float Vbus = mData.Vbus;
        gCatena.SafePrintf("Vbus:    %d mV\n", (int) (Vbus * 1000.0f));
        b.putV(Vbus);
        }

    // send boot count
    if ((this->m_data.flags &  Flags::FlagBoot) !=  Flags(0))
        {
        b.putBootCountLsb(mData.BootCount);
        }

    if ((this->m_data.flags &  Flags::FlagTPH) !=  Flags(0))
        {
        gCatena.SafePrintf(
                "BME280:  T: %d P: %d RH: %d\n",
                (int) mData.env.Temperature,
                (int) mData.env.Pressure,
                (int) mData.env.Humidity
                );
        b.putT(mData.env.Temperature);
        b.putP(mData.env.Pressure);
        b.putRH(mData.env.Humidity);
        }

    // put light
    if ((this->m_data.flags & Flags::FlagLux) != Flags(0))
        {
        gCatena.SafePrintf(
                "Si1133:  %d White\n",
                (int) mData.light.White
                );
        }

    // send compost data
    if ((this->m_data.flags & Flags::FlagWater) !=  Flags(0))
        {
        gCatena.SafePrintf(
                "Compost:  T: %d C\n",
                (int) mData.compost.TempC
                );
        b.putT(mData.compost.TempC);
        }

    gLed.Set(McciCatena::LedPattern::Off);
    }
