# Meter Reading

There are at least two known response payloads sent by the MGM111 chip. MGM Firmware version 2 sends a 152 byte payload while version 7 sends a 44 byte payload. Check the section below relevant to your version.

## Payload for Version 2

The meter reading response contains 152 bytes of payload, most of which seem to always be zeros.  The fields in the payload have
a mix of sizes and just to make things more confusing, the use of LSB / MSB byte ordering is inconsistent.  
The below table details the payload format that's been reversed engineered so far.
Blank cells have never been seen to be anything other than zero.  Note the table is zero-indexed (starts at byte zero, not byte 1)

<table  style="width:20%">
  <tr>   <td></td>
            <th align="center"><img width="50" height="1">0<img width="50" height="1"></th>
            <th align="center"><img width="50" height="1">1<img width="50" height="1"></th>
            <th align="center"><img width="50" height="1">2<img width="50" height="1"></th>
            <th align="center"><img width="50" height="1">3<img width="50" height="1"></th>
  </tr>
  <tr>   <th>0</th> <td colspan=4></td></tr>
  <tr>   <th>4</th> <td colspan=4 align="center">EnergyVal</td></tr>
  <tr>   <th>...</th> <td colspan=4></td></tr>
  <tr>   <th>44</th> <td colspan=3></td><td align="center">MeterDiv</td></tr>
  <tr>   <th>48</th> <td colspan=2></td><td colspan=2 align="center">EnergyCostUnit</td></tr>
  <tr>   <th>52</th> <td colspan=2 align="center">Unknown 1</td><td colspan=2></td></tr>
  <tr>   <th>56</th> <td></td><td colspan=3 align="center">PowerVal</td></tr>
  <tr>   <th>...</th> <td colspan=4></td></tr>
  <tr>   <th>148</th> <td colspan=4 align="center">MeterTS</td></tr>
</table>

### Known Fields

#### EnergyVal

Bytes 4 to 8, 32 bit int, unknown if signed, MSB

Energy Meter totalizer in watts, in other words, cumulative watt-hours consumed.  Unknown when this value resets to zero, 
might reset monthly or on start of new billing cycle.

Sometimes, an invalid number greater than `0x00400000` is returned, it is not understood when or why this happens.

#### MeterDiv

At least byte 47, maybe as large as bytes 44 to 47

Some meters report values not in watts and watt hours but in a multiple of those values.  `EnergyVal` and `PowerVal` should 
be divided by `MeterDiv` to determine the real value.  Usually this is 1, but have also seen a value of 3.

#### EnergyCostUnit

Bytes 50 and 51 MSB(?)

Usually `0x03E8`, which is 1000.  Theorized to be how many `EvergyVal` units per "cost unit" (a value we don't appear to have).
Since people are typically charged per kWh, this value is typically 1000.

This value is not currently used in the code

#### PowerVal

Bytes 57 to 59 (24 bit signed int, MSB)

The power being consumed at the moment in Watts.  If you have a grid-tie solar / wind / battery system, then this value can go negative.
Negative values are returned in 1's complement notation (if left most bit is 1, then flip all the bits then multiply by -1)

"Data Missing" / "Unknown" is denoted as max negative, `0x800000`

#### MeterTS

Bytes 148 to 151 (32 bit unsigned int, LSB)

Number of milliseconds since an unknown event.  Could be time since `EnergyVal` was reset, or could just be a free-running timer.
Will roll over every 49 days.

Only reported as a debugging value, not used in calculations.

#### Unknown 1

Bytes 52 and 53

The meaning of the values in these fields is completely unknown.  They appear to be static for each meter.  Some of the observed values from users include:
```
fbfb
2c2b
3133
```
Random uneducated guess is that this is a bit field with flags about the meter configuration.

## Payload for Version 7

Unlike V2, the V7+ payload is not a proprietary blob. It is a standard
Zigbee Cluster Library (ZCL)
**Read Attributes Response** frame for the **Simple Metering cluster (`0x0702`)**. Rather than reading fields
at fixed byte offsets, we can parse the payload generically as a sequence of ZCL
attribute records. That way, we can handle when attributes are missing or out of order (see
[Variable length](#variable-length-and-missing-attributes) below).

### ZCL frame structure

```
Payload
├── Byte 0        Frame Control            (0x18)
├── Byte 1        Transaction Seq Number   (the "incrementor"; +1 each reading, rolls over)
├── Byte 2        Command ID               (0x01 = Read Attributes Response)
└── Byte 3...     One or more Read-Attribute-Status Records
```

Each **Read-Attribute-Status Record** is:

| Field | Size | Notes |
|---|---|---|
| Attribute Identifier | 2 bytes, LSB | e.g. `0x0000` |
| Status | 1 byte | `0x00` = SUCCESS. Any other value (e.g. `0x86` UNSUPPORTED_ATTRIBUTE) means the record **ends here** — no type or value follows. |
| Attribute Data Type | 1 byte | Only present when status is SUCCESS. ZCL type tag, see below. |
| Attribute Value | *N* bytes, LSB | Only present when status is SUCCESS. *N* is determined by the data type. |

### ZCL data type tags seen in practice

| Tag | Type | Value size |
|---|---|---|
| `0x22` | uint24 | 3 bytes |
| `0x25` | uint48 | 6 bytes |
| `0x2A` | int24 (signed) | 3 bytes |

### Known Attributes (Simple Metering cluster `0x0702`)

#### CurrentSummationDelivered (Attribute `0x0000`)

Format: uint48, LSB

We will call this ImportWh.

Cumulative watt-hours consumed from the grid. Unknown when the value resets, but probably
never resets since the ESP32 never sends a clock sync to the MGM111, so it likely just
rolls over.

#### CurrentSummationReceived (Attribute `0x0001`)

Format: uint48, LSB

We will call this ExportWh.

Cumulative watt-hours sent to the grid. On meters without export/solar this attribute is
potentially reported with status `0x86` (UNSUPPORTED_ATTRIBUTE) and carries no value — see
[Variable length](#variable-length-and-missing-attributes).

#### Multiplier (Attribute `0x0301`)

Format: uint24, LSB

The ZCL Metering *Multiplier*. Combined with the *Divisor* to convert raw summation/demand
values into engineering units.

#### Divisor (Attribute `0x0302`)

Format: uint24, LSB

The ZCL Metering *Divisor*. Usually `1000` (bytes `E8 03 00`).

#### InstantaneousDemand (Attribute `0x0400`)

Format: int24 signed, LSB

We will call this PowerVal, or simply watts.

The power being sent or consumed at this moment. Negative values indicate export. Per the
ZCL Metering spec, the actual value is:

`Value = Raw * Multiplier / Divisor`

Typically, the Divisor has a value of `1000` and Multiplier has a value of `1`, meaning the resulting value becomes kilowatts.

However, I am making the decision for this project that the base unit of all values are in watts (or watt-hours) since that is the true base unit (kilo being a modifier). Therefore, I am turning `Divisor` into `1` by doing `Divisor / 1000`.



### Byte layout of a fully-populated reading

The table below shows a reading where every attribute is present (44 bytes). Zero-indexed.
`FC`/`Incrementor`/`Command` are the ZCL header; `attr`/`status`/`type` are each record's attribute id / status
/ type tag.

<table  style="width:80%">
  <tr>   <td></td>
            <th align="center"><img width="50" height="1">0<img width="50" height="1"></th>
            <th align="center"><img width="50" height="1">1<img width="50" height="1"></th>
            <th align="center"><img width="50" height="1">2<img width="50" height="1"></th>
            <th align="center"><img width="50" height="1">3<img width="50" height="1"></th>
  </tr>
  <tr>   <th>0</th> <td colspan=1 align="center">0x18 FC</td><td colspan=1 align="center">Incrementor</td><td colspan=1 align="center">0x01 Command</td><td colspan=1 align="center">attr 0x00~</td></tr>
  <tr>   <th>4</th> <td colspan=1 align="center">~attr 0x00</td><td colspan=1 align="center">status</td><td colspan=1 align="center">type 0x25</td><td colspan=1 align="center">ImportWh~</td></tr>
  <tr>   <th>8</th> <td colspan=4 align="center">~ImportWh~</td></tr>
  <tr>   <th>12</th> <td colspan=1 align="center">~ImportWh</td><td colspan=2 align="center">attr 0x0001</td><td colspan=1 align="center">status</td></tr>
  <tr>   <th>16</th> <td colspan=1 align="center">type 0x25</td><td colspan=3 align="center">ExportWh~</td></tr>
  <tr>   <th>20</th> <td colspan=3 align="center">~ExportWh</td><td colspan=1 align="center">attr 0x03~</td></tr>
  <tr>   <th>24</th> <td colspan=1 align="center">~attr 0x01</td><td colspan=1 align="center">status</td><td colspan=1 align="center">type 0x22</td><td colspan=1 align="center">Multiplier~</td></tr>
  <tr>   <th>28</th> <td colspan=2 align="center">~Multiplier</td><td colspan=2 align="center">attr 0x0302</td></tr>
  <tr>   <th>32</th> <td colspan=1 align="center">status</td><td colspan=1 align="center">type 0x22</td><td colspan=2 align="center">Divisor~</td></tr>
  <tr>   <th>36</th> <td colspan=1 align="center">~Divisor</td><td colspan=2 align="center">attr 0x0400</td><td colspan=1 align="center">status</td></tr>
  <tr>   <th>40</th> <td colspan=1 align="center">type 0x2A</td><td colspan=3 align="center">PowerVal</td></tr>
</table>

### Variable length and missing attributes

The payload length is **not fixed at 44 bytes**. Any attribute may be reported with a
non-SUCCESS status (commonly `0x86` UNSUPPORTED_ATTRIBUTE), in which case that record is
only 3 bytes long (id + status, no type/value) and the overall payload is shorter. A parser
must therefore walk records bounded by the payload length, not assume a fixed size.