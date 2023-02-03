/*

Module: Model4928_cMeasurementLoop.cpp

Function:
    Class for transmitting accumulated measurements.

Copyright:
    See accompanying LICENSE file for copyright and license information.

Author:
    Dhinesh Kumar Pitchai, MCCI Corporation   May 2022

*/

#include "Model4928_cMeasurementLoop.h"

#include <arduino_lmic.h>
#include <ThermoSense-Lorawan.h>
#include <OneWire.h>
#include <DallasTemperature.h>

using namespace McciModel4928;
using namespace McciCatena;

extern OneWire oneWireOne;
extern OneWire oneWireTwo;
extern DallasTemperature sensor_CompostTempOne;
extern DallasTemperature sensor_CompostTempTwo;
extern cMeasurementLoop gMeasurementLoop;

/****************************************************************************\
|
|   An object to represent the uplink activity
|
\****************************************************************************/

void cMeasurementLoop::begin()
    {
    // register for polling.
    if (! this->m_registered)
        {
        this->m_registered = true;

        gCatena.registerObject(this);

        this->m_UplinkTimer.begin(this->m_txCycleSec * 1000);
        }

    Wire.begin();

    if (this->m_Sht.begin())
        {
        this->m_fSht3x = true;
        gCatena.SafePrintf("SHT3x found: Env sensor\n");
        }
    else
        {
        this->m_fSht3x = false;
        gCatena.SafePrintf("No SHT3x found: check wiring\n");
        }

    if (this->m_Ltr.begin())
        {
        this->m_fHardError = false;
        this->m_fLtr329 = true;
        }
    else
        {
        this->m_fLtr329 = false;
        gCatena.SafePrintf("No LTR329 found: check hardware\n");
        }

    bool fCompostTempOne = this->checkCompostSensorOnePresent();

    if(!fCompostTempOne)
        {
        gCatena.SafePrintf("No one-wire temperature sensor one detected\n");
        }
    else
        {
        gCatena.SafePrintf("One-wire temperature sensor one detected\n");
        }

    bool fCompostTempTwo = this->checkCompostSensorTwoPresent();

    if(!fCompostTempTwo)
        {
        gCatena.SafePrintf("No one-wire temperature sensor two detected\n");
        }
    else
        {
        gCatena.SafePrintf("One-wire temperature sensor two detected\n");
        }

    // start (or restart) the FSM.
    if (! this->m_running)
        {
        this->m_exit = false;
        this->m_fsm.init(*this, &cMeasurementLoop::fsmDispatch);
        }
    }

// return true if the compost sensor one is attached.
bool cMeasurementLoop::checkCompostSensorOnePresent(void)
    {
    /* set D11 high so V_OUT2 is going to be high for onewire sensor */
    pinMode(D11, OUTPUT);
    digitalWrite(D11, HIGH);
    delay(10);

    sensor_CompostTempOne.begin();
    return sensor_CompostTempOne.getDeviceCount() != 0;
    }

// return true if the compost sensor two is attached.
bool cMeasurementLoop::checkCompostSensorTwoPresent(void)
    {
    /* set D10 high so V_OUT1 is going to be high for onewire sensor */
    pinMode(D10, OUTPUT);
    digitalWrite(D10, HIGH);
    delay(10);

    sensor_CompostTempTwo.begin();
    return sensor_CompostTempTwo.getDeviceCount() != 0;
    }

void cMeasurementLoop::end()
    {
    if (this->m_running)
        {
        this->m_exit = true;
        this->m_fsm.eval();
        }
    }

void cMeasurementLoop::requestActive(bool fEnable)
    {
    if (fEnable)
        this->m_rqActive = true;
    else
        this->m_rqInactive = true;

    this->m_fsm.eval();
    }

cMeasurementLoop::State
cMeasurementLoop::fsmDispatch(
    cMeasurementLoop::State currentState,
    bool fEntry
    )
    {
    State newState = State::stNoChange;

    if (fEntry && this->isTraceEnabled(this->DebugFlags::kTrace))
        {
        gCatena.SafePrintf("cMeasurementLoop::fsmDispatch: enter %s\n",
                this->getStateName(currentState)
                );
        }

    switch (currentState)
        {
    case State::stInitial:
        newState = State::stInactive;
        this->resetMeasurements();
        break;

    case State::stInactive:
        if (fEntry)
            {
            // turn off anything that should be off while idling.
            }
        if (this->m_rqActive)
            {
            // when going active manually, start the measurement
            // cycle immediately.
            this->m_rqActive = this->m_rqInactive = false;
            this->m_active = true;
            this->m_UplinkTimer.retrigger();
            newState = State::stWarmup;
            }
        break;

    case State::stSleeping:
        if (fEntry)
            {
            if (!(this->fDisableLED && this->m_fLowLight))
                {
                // set the LEDs to flash accordingly.
                gLed.Set(McciCatena::LedPattern::Sleeping);
                }
            }

        if (this->m_rqInactive)
            {
            this->m_rqActive = this->m_rqInactive = false;
            this->m_active = false;
            newState = State::stInactive;
            }
        else if (this->m_UplinkTimer.isready())
            newState = State::stMeasure;
        else if (this->m_UplinkTimer.getRemaining() > 1500)
            this->sleep();
        break;

      // get some data. This is only called while booting up.
	     case State::stWarmup:
	         if (fEntry)
	             {
	             //start the timer
	             this->setTimer(5 * 1000);
	             }
	         if (this->timedOut())
	             newState = State::stMeasure;
	         break;


    // fill in the measurement
    case State::stMeasure:
		if (fEntry)
            {
            this->updateSynchronousMeasurements();
            this->setTimer(1000);
            }

        if (this->m_fLtr329)
            {
            if (this->m_Ltr.startSingleMeasurement())
                {
                this->updateLightMeasurements();
                newState = State::stTransmit;
                }
            else if (this->timedOut())
                {
                this->m_fHardError = true;
                newState = State::stTransmit;
                }
            else
                this->m_fHardError = true;
            }

        break;

    case State::stTransmit:
        if (fEntry)
            {
            TxBuffer_t b;
            this->fillTxBuffer(b, this->m_data);

            this->m_FileTxBuffer.begin();
            for (auto i = 0; i < b.getn(); ++i)
                this->m_FileTxBuffer.put(b.getbase()[i]);

            this->resetMeasurements();
            this->startTransmission(b);
            }
        if (! gLoRaWAN.IsProvisioned())
            {
            newState = State::stFinal;
            }
        if (this->txComplete())
            {
            newState = State::stSleeping;

            // calculate the new sleep interval.
            this->updateTxCycleTime();
            }
        break;

    case State::stFinal:
        break;

    default:
        break;
        }

    return newState;
    }

/****************************************************************************\
|
|   Take a measurement
|
\****************************************************************************/

void cMeasurementLoop::resetMeasurements()
    {
    memset((void *) &this->m_data, 0, sizeof(this->m_data));
    this->m_data.flags = Flags(0);
    }

void cMeasurementLoop::updateSynchronousMeasurements()
    {
    this->m_data.Vbat = gCatena.ReadVbat();
    this->m_data.flags |= Flags::Vbat;

    this->m_data.Vbus = gCatena.ReadVbus();
    this->m_data.flags |= Flags::Vcc;

    if (gCatena.getBootCount(this->m_data.BootCount))
        {
        this->m_data.flags |= Flags::Boot;
        }

    if (this->m_fSht3x)
        {
        cSHT3x::Measurements m;
        this->m_Sht.getTemperatureHumidity(m);
        this->m_data.env.Temperature = m.Temperature;
        this->m_data.env.Humidity = m.Humidity;
        this->m_data.flags |= Flags::TH;
        }

    /*
    || Measure and transmit the compost temperature (OneWire)
    || tranducer value. This is complicated because we want
    || to support plug/unplug and the sw interface is not
    || really hot-pluggable.
    */

    // enable boost regulator if no USB power and VBat is less than 3.1V
    if (!m_fUsbPower && (this->m_data.Vbat < 3.10f))
        {
        pinMode(D14, OUTPUT);
        digitalWrite(D14, HIGH);
        delay(90);
        }

    bool fCompostTempOne = checkCompostSensorOnePresent();

    if (fCompostTempOne)
        {
        sensor_CompostTempOne.requestTemperatures();
        float compostTempOne = sensor_CompostTempOne.getTempCByIndex(0);
        this->m_data.compost.TempOneC = compostTempOne;
        this->m_data.flags |= Flags::Temp1;
        }
    else if (fHasCompostTemp)
        {
        gCatena.SafePrintf("Not a compost temperature module\n");
        }
    else if(!fCompostTempOne)
        {
        gCatena.SafePrintf("Compost sensor one not detected\n");
        }

    bool fCompostTempTwo = checkCompostSensorTwoPresent();

    if (fCompostTempTwo)
        {
        sensor_CompostTempTwo.requestTemperatures();
        float compostTempTwo = sensor_CompostTempTwo.getTempCByIndex(0);
        this->m_data.compost.TempTwoC = compostTempTwo;
        this->m_data.flags |= Flags::Temp2;
        }
    else if (fHasCompostTemp)
        {
        gCatena.SafePrintf("Not a compost temperature module\n");
        }
    else if(!fCompostTempTwo)
        {
        gCatena.SafePrintf("Compost sensor two not detected\n");
        }

    /* set D11 and D10 low to turn off after measuring */
    pinMode(D11, INPUT);
    pinMode(D10, INPUT);
    pinMode(D14, INPUT);
    }

void cMeasurementLoop::updateLightMeasurements()
    {
    if (this->m_fLtr329)
        {
        float currentLux;
        bool fHardError;

        static constexpr float kMax_Gain_96 = 640.0f;
        static constexpr float kMax_Gain_48 = 1280.0f;
        static constexpr float kMax_Gain_8 = 7936.0f;
        static constexpr float kMax_Gain_4 = 16128.0f;
        static constexpr float kMax_Gain_2 = 32512.0f;
        static constexpr float kMax_Gain_1 = 65535.0f;

        while (! this->m_Ltr.queryReady(fHardError))
            {
            if (fHardError)
                break;
            }

        if (fHardError)
            {
            this->m_fHardError = true;
            if (gLog.isEnabled(gLog.DebugFlags::kError))
                gLog.printf(
                    gLog.kAlways,
                    "LTR329 queryReady failed: status %s(%u)\n",
                    this->m_Ltr.getLastErrorName(),
                    unsigned(this->m_Ltr.getLastError())
                    );
            }
        else
            {
            currentLux = this->m_Ltr.getLux();

            this->m_data.flags |= Flags::Lux;
            this->m_data.light.Lux = currentLux;

            if (currentLux <= kMax_Gain_96)
                m_AlsCtrl.setGain(96);
            else if (currentLux <= kMax_Gain_48)
                m_AlsCtrl.setGain(48);
            else if (currentLux <= kMax_Gain_8)
                m_AlsCtrl.setGain(8);
            else if (currentLux <= kMax_Gain_4)
                m_AlsCtrl.setGain(4);
            else if (currentLux <= kMax_Gain_2)
                m_AlsCtrl.setGain(2);
            else
                m_AlsCtrl.setGain(1);

            if (currentLux <= 100)
                this->m_fLowLight = true;
            else
                this->m_fLowLight = false;
            }
        }
    }

/****************************************************************************\
|
|   Start uplink of data
|
\****************************************************************************/

void cMeasurementLoop::startTransmission(
    cMeasurementLoop::TxBuffer_t &b
    )
    {
    auto const savedLed = gLed.Set(McciCatena::LedPattern::Off);
    if (!(this->fDisableLED && this->m_fLowLight))
        {
        gLed.Set(McciCatena::LedPattern::Sending);
        }

    // by using a lambda, we can access the private contents
    auto sendBufferDoneCb =
        [](void *pClientData, bool fSuccess)
            {
            auto const pThis = (cMeasurementLoop *)pClientData;
            pThis->m_txpending = false;
            pThis->m_txcomplete = true;
            pThis->m_txerr = ! fSuccess;
            pThis->m_fsm.eval();
            };

    bool fConfirmed = false;
    if (gCatena.GetOperatingFlags() &
        static_cast<uint32_t>(gCatena.OPERATING_FLAGS::fConfirmedUplink))
        {
        gCatena.SafePrintf("requesting confirmed tx\n");
        fConfirmed = true;
        }

    this->m_txpending = true;
    this->m_txcomplete = this->m_txerr = false;

    if (! gLoRaWAN.SendBuffer(b.getbase(), b.getn(), sendBufferDoneCb, (void *)this, fConfirmed))
        {
        // uplink wasn't launched.
        this->m_txcomplete = true;
        this->m_txerr = true;
        this->m_fsm.eval();
        }
    }

void cMeasurementLoop::sendBufferDone(bool fSuccess)
    {
    this->m_txpending = false;
    this->m_txcomplete = true;
    this->m_txerr = ! fSuccess;
    this->m_fsm.eval();
    }

/****************************************************************************\
|
|   The Polling function --
|
\****************************************************************************/

void cMeasurementLoop::poll()
    {
    bool fEvent;

    // no need to evaluate unless something happens.
    fEvent = false;

    // if we're not active, and no request, nothing to do.
    if (! this->m_active)
        {
        if (! this->m_rqActive)
            return;

        // we're asked to go active. We'll want to eval.
        fEvent = true;
        }

	   if (this->m_fTimerActive)
	        {
	        if ((millis() - this->m_timer_start) >= this->m_timer_delay)
	            {
	            this->m_fTimerActive = false;
	            this->m_fTimerEvent = true;
	            fEvent = true;
	            }
        }

    // check the transmit time.
    if (this->m_UplinkTimer.peekTicks() != 0)
        {
        fEvent = true;
        }

    if (fEvent)
        this->m_fsm.eval();

    this->m_data.Vbus = gCatena.ReadVbus();
    setVbus(this->m_data.Vbus);
    }

/****************************************************************************\
|
|   Update the TxCycle count.
|
\****************************************************************************/

void cMeasurementLoop::updateTxCycleTime()
    {
    auto txCycleCount = this->m_txCycleCount;

    // update the sleep parameters
    if (txCycleCount > 1)
            {
            // values greater than one are decremented and ultimately reset to default.
            this->m_txCycleCount = txCycleCount - 1;
            }
    else if (txCycleCount == 1)
            {
            // it's now one (otherwise we couldn't be here.)
            gCatena.SafePrintf("resetting tx cycle to default: %u\n", this->m_txCycleSec_Permanent);

            this->setTxCycleTime(this->m_txCycleSec_Permanent, 0);
            }
    else
            {
            // it's zero. Leave it alone.
            }
    }

/****************************************************************************\
|
|   Handle sleep between measurements
|
\****************************************************************************/

void cMeasurementLoop::sleep()
    {
    const bool fDeepSleep = checkDeepSleep();

    if (! this->m_fPrintedSleeping)
            this->doSleepAlert(fDeepSleep);

    if (fDeepSleep)
            this->doDeepSleep();
    }

// for now, we simply don't allow deep sleep. In the future might want to
// use interrupts on activity to wake us up; then go back to sleep when we've
// seen nothing for a while.
bool cMeasurementLoop::checkDeepSleep()
    {
    bool const fDeepSleepTest = gCatena.GetOperatingFlags() &
                    static_cast<uint32_t>(gCatena.OPERATING_FLAGS::fDeepSleepTest);
    bool fDeepSleep;
    std::uint32_t const sleepInterval = this->m_UplinkTimer.getRemaining() / 1000;

    if (sleepInterval < 2)
        fDeepSleep = false;
    else if (fDeepSleepTest)
        {
        fDeepSleep = true;
        }
#ifdef USBCON
    else if (Serial.dtr())
        {
        fDeepSleep = false;
        }
#endif
    else if (gCatena.GetOperatingFlags() &
                static_cast<uint32_t>(gCatena.OPERATING_FLAGS::fDisableDeepSleep))
        {
        fDeepSleep = false;
        }
    else if ((gCatena.GetOperatingFlags() &
                static_cast<uint32_t>(gCatena.OPERATING_FLAGS::fUnattended)) != 0)
        {
        fDeepSleep = true;
        }
    else
        {
        fDeepSleep = false;
        }

    return fDeepSleep;
    }

void cMeasurementLoop::doSleepAlert(bool fDeepSleep)
    {
    this->m_fPrintedSleeping = true;

    if (fDeepSleep)
        {
        bool const fDeepSleepTest =
                gCatena.GetOperatingFlags() &
                    static_cast<uint32_t>(gCatena.OPERATING_FLAGS::fDeepSleepTest);
        const uint32_t deepSleepDelay = fDeepSleepTest ? 10 : 30;

        gCatena.SafePrintf("using deep sleep in %u secs"
#ifdef USBCON
                            " (USB will disconnect while asleep)"
#endif
                            ": ",
                            deepSleepDelay
                            );

        if (!(this->fDisableLED && this->m_fLowLight))
            {
            // sleep and print
            gLed.Set(McciCatena::LedPattern::TwoShort);
            }

        for (auto n = deepSleepDelay; n > 0; --n)
            {
            uint32_t tNow = millis();

            while (uint32_t(millis() - tNow) < 1000)
                {
                gCatena.poll();
                yield();
                }
            gCatena.SafePrintf(".");
            }
        gCatena.SafePrintf("\nStarting deep sleep.\n");
        uint32_t tNow = millis();
        while (uint32_t(millis() - tNow) < 100)
            {
            gCatena.poll();
            yield();
            }
        }
    else
        gCatena.SafePrintf("using light sleep\n");
    }

void cMeasurementLoop::doDeepSleep()
    {
    // bool const fDeepSleepTest = gCatena.GetOperatingFlags() &
    //                         static_cast<uint32_t>(gCatena.OPERATING_FLAGS::fDeepSleepTest);
    std::uint32_t const sleepInterval = this->m_UplinkTimer.getRemaining() / 1000;

    if (sleepInterval == 0)
        return;

    /* ok... now it's time for a deep sleep */
    gLed.Set(McciCatena::LedPattern::Off);
    this->deepSleepPrepare();

    /* sleep */
    gCatena.Sleep(sleepInterval);

    /* recover from sleep */
    this->deepSleepRecovery();

    /* and now... we're awake again. trigger another measurement */
    this->m_fsm.eval();
    }

void cMeasurementLoop::deepSleepPrepare(void)
    {
    Serial.end();
    Wire.end();
    SPI.end();
    if (this->m_pSPI2 && this->m_fSpi2Active)
        {
        this->m_pSPI2->end();
        this->m_fSpi2Active = false;
        }
    pinMode(D11, INPUT);
    pinMode(D10, INPUT);
    }

void cMeasurementLoop::deepSleepRecovery(void)
    {
    pinMode(D11, OUTPUT);
    digitalWrite(D11, HIGH);
    delay(10);

    pinMode(D10, OUTPUT);
    digitalWrite(D10, HIGH);
    delay(10);

    Serial.begin();
    Wire.begin();
    SPI.begin();
    //if (this->m_pSPI2)
    //    this->m_pSPI2->begin();
    }

/****************************************************************************\
|
|  Time-out asynchronous measurements.
|
\****************************************************************************/

// set the timer
void cMeasurementLoop::setTimer(std::uint32_t ms)
    {
    this->m_timer_start = millis();
    this->m_timer_delay = ms;
    this->m_fTimerActive = true;
    this->m_fTimerEvent = false;
    }

void cMeasurementLoop::clearTimer()
    {
    this->m_fTimerActive = false;
    this->m_fTimerEvent = false;
    }

bool cMeasurementLoop::timedOut()
    {
    bool result = this->m_fTimerEvent;
    this->m_fTimerEvent = false;
    return result;
    }
