/*

Name:   catena-message-0x2a-port-1-decoder-nodered.js

Function:
    This function decodes the record (port 1, format 0x27) sent by the
    MCCI Model 4928 Temperature Sensor application.

Copyright and License:
    See accompanying LICENSE file at ---

Author:
    Pranau R, MCCI Corporation   February 2023

*/

// calculate dewpoint (degrees C) given temperature (C) and relative humidity (0..100)
// from http://andrew.rsmas.miami.edu/bmcnoldy/Humidity.html
// rearranged for efficiency and to deal sanely with very low (< 1%) RH
function dewpoint(t, rh) {
    var c1 = 243.04;
    var c2 = 17.625;
    var h = rh / 100;
    if (h <= 0.01)
        h = 0.01;
    else if (h > 1.0)
        h = 1.0;

    var lnh = Math.log(h);
    var tpc1 = t + c1;
    var txc2 = t * c2;
    var txc2_tpc1 = txc2 / tpc1;

    var tdew = c1 * (lnh + txc2_tpc1) / (c2 - lnh - txc2_tpc1);
    return tdew;
}

/*

Name:   CalculateHeatIndex()

Description:
        Calculate the NWS heat index given dry-bulb T and RH

Definition:
        function CalculateHeatIndex(t, rh) -> value or null

Description:
        T is a Farentheit temperature in [76,120]; rh is a
        relative humidity in [0,100]. The heat index is computed
        and returned; or an error is returned.

Returns:
        number => heat index in Farenheit.
        null => error.

References:
        https://github.com/mcci-catena/heat-index/
        https://www.wpc.ncep.noaa.gov/html/heatindex_equation.shtml

        Results was checked against the full chart at iweathernet.com:
        https://www.iweathernet.com/wxnetcms/wp-content/uploads/2015/07/heat-index-chart-relative-humidity-2.png

        The MCCI-Catena heat-index site has a test js script to generate CSV to
        match the chart, a spreadsheet that recreates the chart, and a
        spreadsheet that compares results.

*/

function CalculateHeatIndex(t, rh) {
    var tRounded = Math.floor(t + 0.5);

    // return null outside the specified range of input parameters
    if (tRounded < 76 || tRounded > 126)
        return null;
    if (rh < 0 || rh > 100)
        return null;

    // according to the NWS, we try this first, and use it if we can
    var tHeatEasy = 0.5 * (t + 61.0 + ((t - 68.0) * 1.2) + (rh * 0.094));

    // The NWS says we use tHeatEasy if (tHeatHeasy + t)/2 < 80.0
    // This is the same computation:
    if ((tHeatEasy + t) < 160.0)
            return tHeatEasy;

    // need to use the hard form, and possibly adjust.
    var t2 = t * t;         // t squared
    var rh2 = rh * rh;      // rh squared
    var tResult =
        -42.379 +
        (2.04901523 * t) +
        (10.14333127 * rh) +
        (-0.22475541 * t * rh) +
        (-0.00683783 * t2) +
        (-0.05481717 * rh2) +
        (0.00122874 * t2 * rh) +
        (0.00085282 * t * rh2) +
        (-0.00000199 * t2 * rh2);

    // these adjustments come from the NWA page, and are needed to
    // match the reference table.
    var tAdjust;
    if (rh < 13.0 && 80.0 <= t && t <= 112.0)
        tAdjust = -((13.0 - rh) / 4.0) * Math.sqrt((17.0 - Math.abs(t - 95.0)) / 17.0);
    else if (rh > 85.0 && 80.0 <= t && t <= 87.0)
        tAdjust = ((rh - 85.0) / 10.0) * ((87.0 - t) / 5.0);
    else
        tAdjust = 0;

    // apply the adjustment
    tResult += tAdjust;

    // finally, the reference tables have no data above 183 (rounded),
    // so filter out answers that we have no way to vouch for.
    if (tResult >= 183.5)
        return null;
    else
        return tResult;
}

function CalculateHeatIndexCelsius(t, rh) {
    var result = CalculateHeatIndex(t, rh);
    if (result !== null) {
        // convert to celsius.
        result = (result - 32) * 5 / 9;
    }
    return result;
}

function DecodeU16(Parse) {
    var i = Parse.i;
    var bytes = Parse.bytes;
    var raw = (bytes[i] << 8) + bytes[i + 1];
    Parse.i = i + 2;
    return raw;
}

function DecodeI16(Parse) {
    var Vraw = DecodeU16(Parse);

    // interpret uint16 as an int16 instead.
    if (Vraw & 0x8000)
        Vraw += -0x10000;

    return Vraw;
}

function DecodeU24(Parse) {
    var i = Parse.i;
    var bytes = Parse.bytes;

    var result = (bytes[i + 0] << 16) + (bytes[i + 1] << 8) + bytes[i + 2];
    Parse.i = i + 3;

    return result;
}

function DecodeSflt24(Parse)
    {
    var rawSflt24 = DecodeU24(Parse);
    // rawSflt24 is the 3-byte number decoded from wherever;
    // it's in range 0..0xFFFFFF
    // bit 23 is the sign bit
    // bits 22..16 are the exponent
    // bits 15..0 are the the mantissa. Unlike IEEE format,
    // the msb is explicit; this means that numbers
    // might not be normalized, but makes coding for
    // underflow easier.
    // As with IEEE format, negative zero is possible, so
    // we special-case that in hopes that JavaScript will
    // also cooperate.

    // extract sign, exponent, mantissa
    var bSign     = (rawSflt24 & 0x800000) ? true : false;
    var uExp      = (rawSflt24 & 0x7F0000) >> 16;
    var uMantissa = (rawSflt24 & 0x00FFFF);

    // if non-numeric, return appropriate result.
    if (uExp === 0x7F) {
        if (uMantissa === 0)
            return bSign ? Number.NEGATIVE_INFINITY
                    : Number.POSITIVE_INFINITY;
        else
            return Number.NaN;
    // else unless denormal, set the 1.0 bit
    } else if (uExp !== 0) {
        uMantissa += 0x010000;
    } else { // denormal: exponent is the minimum
        uExp = 1;
    }

    // make a floating mantissa in [0,2); usually [1,2), but
    // sometimes (0,1) for denormals, and exactly zero for zero.
    var mantissa = uMantissa / 0x010000;

    // apply the exponent.
    mantissa = Math.pow(2, uExp - 63) * mantissa;

    // apply sign and return result.
    return bSign ? -mantissa : mantissa;
    }

function DecodeLux(Parse) {
    return DecodeSflt24(Parse);
}

function DecodeV(Parse) {
    return DecodeI16(Parse) / 4096.0;
}

function Decoder(bytes, port) {
    // Decode an uplink message from a buffer
    // (array) of bytes to an object of fields.
    var decoded = {};

    if (! (port === 1))
        return null;

    var uFormat = bytes[0];
    if (! (uFormat === 0x2a))
        return null;

    // an object to help us parse.
    var Parse = {};
    Parse.bytes = bytes;
    // i is used as the index into the message. Start with the flag byte.
    Parse.i = 1;

    // fetch the bitmap.
    var flags = bytes[Parse.i++];

    if (flags & 0x1) {
        decoded.vBat = DecodeV(Parse);
    }

    if (flags & 0x2) {
        decoded.vBus = DecodeV(Parse);
    }

    if (flags & 0x4) {
        var iBoot = bytes[Parse.i++];
        decoded.boot = iBoot;
    }

    if (flags & 0x8) {
        // we have temp, RH
        decoded.tempC = DecodeI16(Parse) / 256;
        decoded.rh = DecodeU16(Parse) * 100 / 65535.0;
        decoded.tDewC = dewpoint(decoded.tempC, decoded.rh);
        var tHeat = CalculateHeatIndex(decoded.tempC * 1.8 + 32, decoded.rh);
        if (tHeat !== null)
            decoded.tHeatIndexC = tHeat;
    }

    if (flags & 0x10) {
        // we have light
        decoded.lux = DecodeLux(Parse);
    }

    if (flags & 0x20) {
        // onewire temperature
        decoded.tProbeOne = DecodeI16(Parse) / 256;
    }

    if (flags & 0x40) {
        // onewire temperature
        decoded.tProbeTwo = DecodeI16(Parse) / 256;
    }

    return decoded;
}

// end of insertion of catena-message-0x2a-port-1-decoder-nodered.js

/*

Node-RED function body.

Input:
    msg     the object to be decoded.

            msg.payload_raw is taken
            as the raw payload if present; otheriwse msg.payload
            is taken to be a raw payload.

            msg.port is taken to be the LoRaWAN port nubmer.


Returns:
    This function returns a message body. It's a mutation of the
    input msg; msg.payload is changed to the decoded data, and
    msg.local is set to additional application-specific information.

*/

var bytes;

if ("payload_raw" in msg) {
    // the console already decoded this
    bytes = msg.payload_raw;  // pick up data for convenience
    // msg.payload_fields still has the decoded data from ttn
} else {
    // no console decode
    bytes = msg.payload;  // pick up data for conveneince
}

// try to decode.
var result = Decoder(bytes, msg.port);

if (result === null) {
    // not one of ours: report an error, return without a value,
    // so that Node-RED doesn't propagate the message any further.
    var eMsg = "not port 1/fmt 0x2a! port=" + msg.port.toString();
    if (port === 2) {
        if (Buffer.byteLength(bytes) > 0) {
            eMsg = eMsg + " fmt=" + bytes[0].toString();
        } else {
            eMsg = eMsg + " <no fmt byte>"
        }
    }
    node.error(eMsg);
    return;
}

// now update msg with the new payload and new .local field
// the old msg.payload is overwritten.
msg.payload = result;
msg.local =
    {
        nodeType: "Model 4928",
        platformType: "Catena 4610",
        radioType: "Murata",
        applicationName: "Temperature Sensor"
    };

return msg;
