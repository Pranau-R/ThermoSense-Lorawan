# Understanding Model4928 Temperature Sensor data format 0x2a

<!-- TOC depthFrom:2 updateOnSave:true -->

- [Overall Message Format](#overall-message-format)
- [Field format definitions](#field-format-definitions)
	- [Battery Voltage (field 0)](#battery-voltage-field-0)
	- [Bus Voltage (field 1)](#bus-voltage-field-1)
	- [Boot counter (field 2)](#boot-counter-field-2)
	- [Environmental Readings (field 3)](#environmental-readings-field-3)
	- [Lux Readings (field 4)](#lux-readings-field-4)
	- [Temperature Probe One (field 5)](#temperature-probe-one-field-5)
	- [Temperature Probe Two (field 6)](#temperature-probe-two-field-6)
- [Data Formats](#data-formats)
	- [uint16](#uint16)
	- [int16](#int16)
	- [uint8](#uint8)
	- [uflt16](#uflt16)

<!-- /TOC -->

## Overall Message Format

MCCI Model 4928 - Temperature Sensor format 0x2a messages are always sent on LoRaWAN port 1. Each message has the following layout.

byte | description
:---:|:---
0 | Format code (always 0x2a, decimal 21).
1 | bitmap encoding the fields that follow
2..n | data bytes; use bitmap to decode.

Each bit in byte 1 represent whether a corresponding field in bytes 2..n is present. If all bits are clear, then no data bytes are present. If bit 0 is set, then field 0 is present; if bit 1 is set, then field 1 is present, and so forth.

Fields are appended sequentially in ascending order.  A bitmap of 0000101 indicates that field 0 is present, followed by field 2; the other fields are missing.  A bitmap of 00011010 indicates that fields 1, 3, and 4 are present, in that order, but that fields 0, 2, 5 and 6 are missing.

## Field format definitions

Each field has its own format, as defined in the following table. `int16`, `uint16`, etc. are defined after the table.

Field number (Bitmap bit) | Length of corresponding field (bytes) | Data format |Description
:---:|:---:|:---:|:----
0 | 2 | [int16](#int16) | [Battery voltage](#battery-voltage-field-0)
1 | 2 | [int16](#int16) | [Bus voltage](#bus-voltage-field-1)
2 | 1 | [uint8](#uint8) | [Boot counter](#boot-counter-field-2)
3 | 4 | [int16,](#int16) [uint16](#uint16) | [Temperature, humidity](#environmental-readings-field-3)
4 | 2 | [uflt16](#uflt16) | [Lux](#lux-readings-field-4)
5 | 2 | [int16](#int16) | [Temperature Probe One](#temperature-probe-one-field-5)
6 | 2 | [int16](#int16) | [Temperature Probe Two](#temperature-probe-two-field-6)
7 | n/a | n/a | reserved, must always be zero.

### Battery Voltage (field 0)

Field 0, if present, carries the current battery voltage. To get the voltage, extract the int16 value, and divide by 4096.0. (Thus, this field can represent values from -8.0 volts to 7.998 volts.)

### Bus Voltage (field 1)

Field 1, if present, carries the current voltage from USB VBus. Divide by 4096.0 to convert from counts to volts. (Thus, this field can represent values from -8.0 volts to 7.998 volts.)

_Note:_ this field is not transmitted by some versions of the sketches.

### Boot counter (field 2)

Field 2, if present, is a counter of number of recorded system reboots, modulo 256.

### Environmental Readings (field 3)

Field 3, if present, has three environmental readings as four bytes of data.

- The first two bytes are a [`int16`](#int16) representing the temperature (divide by 256 to get degrees Celsius).

- The next two bytes are a [`uint16`](#uint16) representing the relative humidity (divide by 65535 to get percent). This field can represent humidity from 0% to 100%, in steps of roughly 0.001529%.

### Lux Readings (field 4)

Field 4, if present, directly represents the ambient light level in lux as a [`uflt16`](#uflt16).

### Temperature Probe One (field 5)

Field 5, if present, represents the temperature measured from the external temperature probe one.  This is an [`int16`](#int16) representing the temperature (divide by 256 to get degrees C).

### Temperature Probe Two (field 6)

Field 6, if present, represents the temperature measured from the external temperature probe two.  This is an [`int16`](#int16) representing the temperature (divide by 256 to get degrees C).

## Data Formats

All multi-byte data is transmitted with the most significant byte first (big-endian format).  Comments on the individual formats follow.

### `uint16`

an integer from 0 to 65536.

### `int16`

a signed integer from -32,768 to 32,767, in two's complement form. (Thus 0..0x7FFF represent 0 to 32,767; 0x8000 to 0xFFFF represent -32,768 to -1).

### `uint8`

an integer from 0 to 255.

### `uflt16`

An unsigned floating point number in the half-open range [0, 1), transmitted as a 16-bit number with the following interpretation:

bits | description
:---:|:---
15..12 | binary exponent `b`
11..0 | fraction `f`

The floating point number is computed by computing `f`/4096 * 2^(`b`-15). Note that this format is deliberately not IEEE-compliant; it's intended to be easy to decode by hand and not overwhelmingly sophisticated.

For example, if the transmitted message contains 0x1A, 0xAB, the equivalent floating point number is found as follows.

1. The full 16-bit number is 0x1AAB.
2. `b`  is therefore 0x1, and `b`-15 is -14.  2^-14 is 1/32768
3. `f` is 0xAAB. 0xAAB/4096 is 0.667
4. `f * 2^(b-15)` is therefore 0.6667/32768 or 0.0000204

Floating point mavens will immediately recognize:

* There is no sign bit; all numbers are positive.
* Numbers do not need to be normalized (although in practice they always are).
* The format is somewhat wasteful, because it explicitly transmits the most-significant bit of the fraction. (Most binary floating-point formats assume that `f` is is normalized, which means by definition that the exponent `b` is adjusted and `f` is shifted left until the most-significant bit of `f` is one. Most formats then choose to delete the most-significant bit from the encoding. If we were to do that, we would insist that the actual value of `f` be in the range 2048.. 4095, and then transmit only `f - 2048`, saving a bit. However, this complicated the handling of gradual underflow; see next point.)
* Gradual underflow at the bottom of the range is automatic and simple with this encoding; the more sophisticated schemes need extra logic (and extra testing) in order to provide the same feature.
