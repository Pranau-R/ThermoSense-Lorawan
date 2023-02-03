/*

Module: ThermoSense-Lorawan.ino

Function:
    Code for the ThermoSense sensor based on Catena 4610

Copyright:
    See accompanying LICENSE file for copyright and license information.

Author:
    Dhinesh Kumar Pitchai, MCCI Corporation   May 2022

*/

#include <Arduino.h>
#include <Wire.h>
#include <Catena.h>
#include <arduino_lmic.h>
#include <Catena_Timer.h>
#include "ThermoSense-Lorawan.h"
#include <OneWire.h>
#include <DallasTemperature.h>
#include "Model4928_cMeasurementLoop.h"
#include "Model4928_cmd.h"

extern McciCatena::Catena gCatena;
using namespace McciModel4928;
using namespace McciCatena;
using namespace McciCatenaSht3x;

static_assert(
    CATENA_ARDUINO_PLATFORM_VERSION_COMPARE_GE(
        CATENA_ARDUINO_PLATFORM_VERSION,
        CATENA_ARDUINO_PLATFORM_VERSION_CALC(0, 21, 0, 5)
        ),
    "This sketch requires Catena-Arduino-Platform v0.21.0-5 or later"
    );

static const char sVersion[] = "1.0.0";

#define PIN_A2_ONE_WIRE     A2
#define PIN_D12_ONE_WIRE    D12

/****************************************************************************\
|
|   Variables.
|
\****************************************************************************/
Catena gCatena;
cTimer ledTimer;
Catena::LoRaWAN gLoRaWAN;
StatusLed gLed (Catena::PIN_STATUS_LED);

// the Temperature/Humidity Sensor
cSHT3x gSht { Wire };

// the light Sensor
Ltr_329als gLtr {Wire};

// the measurement loop instance
cMeasurementLoop gMeasurementLoop { gSht, gLtr };

/* instantiate SPI */
SPIClass gSPI2(
		Catena::PIN_SPI2_MOSI,
		Catena::PIN_SPI2_MISO,
		Catena::PIN_SPI2_SCK
		);

/* instantiate the flash */
Catena_Mx25v8035f gFlash;

OneWire oneWireOne(PIN_A2_ONE_WIRE);
OneWire oneWireTwo(PIN_D12_ONE_WIRE);
DallasTemperature sensor_CompostTempOne(&oneWireOne);
DallasTemperature sensor_CompostTempTwo(&oneWireTwo);
bool fHasCompostTemp;

/****************************************************************************\
|
|   User commands
|
\****************************************************************************/

// the individual commmands are put in this table
static const cCommandStream::cEntry sMyExtraCommmands[] =
        {
        { "log", cmdLog },
        // other commands go here....
        };

/* a top-level structure wraps the above and connects to the system table */
/* it optionally includes a "first word" so you can for sure avoid name clashes */
static cCommandStream::cDispatch
sMyExtraCommands_top(
        sMyExtraCommmands,          /* this is the pointer to the table */
        sizeof(sMyExtraCommmands),  /* this is the size of the table */
        nullptr                     /* this is no "first word" for all the commands in this table */
        );


/****************************************************************************\
|
|   Setup
|
\****************************************************************************/

void setup()
    {
    setup_platform();
    setup_printSignOn();

    setup_flash();
    setup_measurement();
    setup_radio();
    setup_commands();
    setup_start();
    }

void setup_platform()
    {
    gCatena.begin();

    // if running unattended, don't wait for USB connect.
    if (! (gCatena.GetOperatingFlags() &
        static_cast<uint32_t>(gCatena.OPERATING_FLAGS::fUnattended)))
        {
        while (!Serial)
            /* wait for USB attach */
            yield();
        }
    }

static constexpr const char *filebasename(const char *s)
    {
    const char *pName = s;

    for (auto p = s; *p != '\0'; ++p)
        {
        if (*p == '/' || *p == '\\')
            pName = p + 1;
        }
    return pName;
    }

void setup_printSignOn()
    {
    static const char dashes[] = "------------------------------------";

    gCatena.SafePrintf("\n%s%s\n", dashes, dashes);

    gCatena.SafePrintf("This is %s v%s.\n",
        filebasename(__FILE__),
        sVersion
        );

    do
        {
        char sRegion[16];
        gCatena.SafePrintf("Target network: %s / %s\n",
                        gLoRaWAN.GetNetworkName(),
                        gLoRaWAN.GetRegionString(sRegion, sizeof(sRegion))
                        );
        } while (0);

    gCatena.SafePrintf("System clock rate is %u.%03u MHz\n",
        ((unsigned)gCatena.GetSystemClockRate() / (1000*1000)),
        ((unsigned)gCatena.GetSystemClockRate() / 1000 % 1000)
        );
    gCatena.SafePrintf("Enter 'help' for a list of commands.\n");
    gCatena.SafePrintf("%s%s\n" "\n", dashes, dashes);

    Catena::UniqueID_string_t CpuIDstring;

    gCatena.SafePrintf(
            "CPU Unique ID: %s\n",
            gCatena.GetUniqueIDstring(&CpuIDstring)
            );

    /* find the platform */
    const Catena::EUI64_buffer_t *pSysEUI = gCatena.GetSysEUI();

    uint32_t flags;
    const CATENA_PLATFORM * const pPlatform = gCatena.GetPlatform();

    if (pPlatform)
        {
        gCatena.SafePrintf("EUI64: ");
        for (unsigned i = 0; i < sizeof(pSysEUI->b); ++i)
            {
            gCatena.SafePrintf("%s%02x", i == 0 ? "" : "-", pSysEUI->b[i]);
            }
        gCatena.SafePrintf("\n");
        flags = gCatena.GetPlatformFlags();
        gCatena.SafePrintf("Platform Flags:  %#010x\n", flags);
        gCatena.SafePrintf("Operating Flags:  %#010x\n",
            gCatena.GetOperatingFlags()
            );
        }
    else
        {
        gCatena.SafePrintf("**** no platform, check provisioning ****\n");
        flags = 0;
        }

    /* is it modded? */
    uint32_t modnumber = gCatena.PlatformFlags_GetModNumber(flags);

    /* modnumber is 102 for WeRadiate app */
    if (modnumber != 0)
        {
        gCatena.SafePrintf("Catena 4610-M%u\n", modnumber);
        if (modnumber == 102)
            {
            fHasCompostTemp = flags & CatenaBase::fHasWaterOneWire;
            }
        else
            {
            gCatena.SafePrintf("unknown mod number %d\n", modnumber);
            }
        }
    else
        {
        gCatena.SafePrintf("No mods detected\n");
        }
    }

void setup_flash(void)
    {
    gSPI2.begin();
    if (gFlash.begin(&gSPI2, Catena::PIN_SPI2_FLASH_SS))
        {
        gMeasurementLoop.registerSecondSpi(&gSPI2);
        gFlash.powerDown();
        gCatena.SafePrintf("FLASH found, put power down\n");
        }
    else
        {
        gFlash.end();
        gSPI2.end();
        gCatena.SafePrintf("No FLASH found: check hardware\n");
        }
    }

void setup_radio()
    {
    gLoRaWAN.begin(&gCatena);
    gCatena.registerObject(&gLoRaWAN);
    LMIC_setClockError(5 * MAX_CLOCK_ERROR / 100);
    }

void setup_measurement()
    {
    gMeasurementLoop.begin();
    }

void setup_commands()
    {
    /* add our application-specific commands */
    gCatena.addCommands(
        /* name of app dispatch table, passed by reference */
        sMyExtraCommands_top,
        /*
        || optionally a context pointer using static_cast<void *>().
        || normally only libraries (needing to be reentrant) need
        || to use the context pointer.
        */
        nullptr
        );
    }

void setup_start()
    {
    gMeasurementLoop.requestActive(true);
    }

/****************************************************************************\
|
|   Loop
|
\****************************************************************************/

void loop()
    {
    gCatena.poll();
    }
