/*

Module: Model4928_cMeasurementLoop_fillBuffer.cpp

Function:
    Class for transmitting accumulated measurements.

Copyright:
    See accompanying LICENSE file for copyright and license information.

Author:
    Dhinesh Kumar Pitchai, MCCI Corporation   May 2022

*/

#include <Catena_TxBuffer.h>

#include "Model4928_cMeasurementLoop.h"

#include <arduino_lmic.h>

using namespace McciCatena;
using namespace McciModel4928;

/*

Name:   McciModel4928::cMeasurementLoop::fillTxBuffer()

Function:
    Prepare a messages in a TxBuffer with data from current measurements.

Definition:
    void McciModel4928::cMeasurementLoop::fillTxBuffer(
            cMeasurementLoop::TxBuffer_t& b
            );

Description:
    A format 0x2a message is prepared from the data in the cMeasurementLoop
    object.

*/

void
cMeasurementLoop::fillTxBuffer(
    cMeasurementLoop::TxBuffer_t& b, Measurement const &mData
    )
    {
    auto const savedLed = gLed.Set(McciCatena::LedPattern::Off);
    if (!(this->fDisableLED && this->m_fLowLight))
        {
        gLed.Set(McciCatena::LedPattern::Measuring);
        }

    // initialize the message buffer to an empty state
    b.begin();

    // insert format byte
    b.put(kMessageFormat);

    // the flags in Measurement correspond to the over-the-air flags.
    b.put(std::uint8_t(this->m_data.flags));

    // send Vbat
    if ((this->m_data.flags & Flags::Vbat) !=  Flags(0))
        {
        float Vbat = mData.Vbat;
        gCatena.SafePrintf("Vbat:    %d mV\n", (int) (Vbat * 1000.0f));
        b.putV(Vbat);
        }

    // send Vdd if we can measure it.

    // Vbus is sent as 5000 * v
    if ((this->m_data.flags & Flags::Vcc) !=  Flags(0))
        {
        float Vbus = mData.Vbus;
        gCatena.SafePrintf("Vbus:    %d mV\n", (int) (Vbus * 1000.0f));
        b.putV(Vbus);
        }

    // send boot count
    if ((this->m_data.flags & Flags::Boot) !=  Flags(0))
        {
        b.putBootCountLsb(mData.BootCount);
        }

    if ((this->m_data.flags & Flags::TH) != Flags(0))
        {
        if (this->m_fSht3x)
            {
            gCatena.SafePrintf(
                    "SHT3x      :  T: %d RH: %d\n",
                    (int) mData.env.Temperature,
                    (int) mData.env.Humidity
                    );
            b.putT(mData.env.Temperature);
            // no method for 2-byte RH, directly encode it.
            b.put2uf((mData.env.Humidity / 100.0f) * 65535.0f);
            }
        }

    // put light
    if ((this->m_data.flags & Flags::Lux) != Flags(0))
        {
        gCatena.SafePrintf(
                "Ltr329:  %d Lux\n",
                (int) mData.light.Lux
                );

        b.put3f(mData.light.Lux);
        }

    // send compost one data
    if ((this->m_data.flags & Flags::Temp1) !=  Flags(0))
        {
        gCatena.SafePrintf(
                "CompostOne:  T: %d C\n",
                (int) mData.compost.TempOneC
                );
        b.putT(mData.compost.TempOneC);
        }

    // send compost two data
    if ((this->m_data.flags & Flags::Temp2) !=  Flags(0))
        {
        gCatena.SafePrintf(
                "CompostTwo:  T: %d C\n",
                (int) mData.compost.TempTwoC
                );
        b.putT(mData.compost.TempTwoC);
        }

    if (!(this->fDisableLED && this->m_fLowLight))
        gLed.Set(savedLed);
    }
