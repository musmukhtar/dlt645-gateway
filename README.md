# dlt645-gateway

A portable C library that reads a Sanxing prepaid electricity meter over DL/T 645-1997, with a thin gateway layer on top so that code above it never has to know which brand of meter is on the other end of the wire.

Every decoder here is a pure function over a byte buffer. No serial I/O, no heap, no dependencies past the C standard library. The whole thing builds and runs its tests on a PC in about a second, which is the only reason the protocol got worked out at all.

## Status

Reading works. It is tested against 39 recorded request/response pairs captured from a real meter, and `make test` checks 26 assertions against values that were confirmed independently (photographs of the meter's own display, amounts recorded on video while tokens were entered). The ASan and UBSan build is clean.

Not built yet: pulling bytes off a UART into complete frames, sending commands to the meter, and forwarding readings anywhere. Those are deliberate gaps with defined edges, not half-finished code.

## Why it is built this way

Nothing here touches a serial port, so the 39 captures work as a regression suite that runs on a laptop in a second. That is not a side benefit. It is the reason the protocol got worked out at all: every hypothesis about what a byte meant could be tested against 39 real frames immediately, and a wrong guess showed up as a failing assertion rather than as a confusing reading three weeks later on a wall in the field.

It runs on small hardware. No heap allocation anywhere, no dependency past the C standard library, and the decoders keep values as an integer with its divisor, so voltage stays as 2390 and 10 rather than becoming a float. The frame struct is 96 bytes and owns its own unmasking buffer, so a caller never manages scratch memory. Floating point appears only at the top layer, where a host that has an FPU can afford it.

Serial input is treated as hostile. The length byte arrives from the wire, so it is bounds-checked before it is used for anything, and a frame claiming more data than the buffer holds is rejected rather than copied. The input buffer is const throughout; unmasking writes to the decoder's own storage. This is worth stating because the reference DL/T 645 implementation this was checked against gets both of those wrong: it fills a stack buffer using a length taken straight off the wire, and it walks a cursor past the end of the input on a truncated frame.

Unrecognised data stays raw instead of becoming a wrong number. If a data identifier is not in the table, the decoder still reports the address, the status, the credit and the byte range of the value, and simply declines to interpret it. A stock DL/T 645 table would instead decode the same bytes as a date and return success. Silence about something unknown is more useful than a confident wrong answer, particularly when the value ends up on a customer's bill.

Extending it is mostly data entry. A newly understood reading is one row in a table, not new code. A new meter brand is one adapter file plus two lines in the registry. The layers exist so that a fresh capture that overturns a guess only edits the vendor layer and cannot disturb the framing code that already works.

The meter brand is chosen once at startup rather than sniffed on every frame, which suits the deployment: each installed gateway is wired to one meter for its whole life. Re-deciding per frame would mean a corrupted frame could change the gateway's mind about what it is talking to.

## The meter

| | |
| --- | --- |
| Unit | CIU-MH03, Ningbo Sanxing Electric |
| Standards printed on the case | STS 2.0, IEC 62055-41/51, IEC 62056-21 |
| Protocol actually on the wire | DL/T 645-1997 |
| Serial settings | 2400 baud, 8E1 |

This began as an M-Bus (EN 13757) decoding job. That identification was wrong, and an M-Bus decoder gets nowhere against these frames.

## Build

```sh
make test    # 26 assertions against the captures
make run     # decode all 39 captures and print the readings
make debug   # same tests under AddressSanitizer and UndefinedBehaviorSanitizer
make clean
```

Needs a C11 compiler and nothing else.

## Using it

Pick a driver once at startup, then feed it frames:

```c
#include "gw/gw_protocol.h"

gw_session_t session;
gw_bind(&session, GW_PROTO_SANXING);        /* or gw_bind_auto() with a sample frame */

gw_reading_t readings[4];
size_t count = 0;

if (gw_decode(&session, frame, frame_len, readings, 4, &count) == GW_OK) {
    for (size_t i = 0; i < count; i++) {
        if (readings[i].has_value) {
            printf("%s: %.3f %s\n", readings[i].label,
                                    readings[i].value,
                                    readings[i].unit);
        }
    }
}
```

One frame can produce several readings. A long reply carries the value you asked for plus two standing registers (remaining credit and lifetime energy) that the meter appends to everything, so a voltage query comes back as three readings.

`apps/demo.c` is a working example that runs against the recorded captures.

## How the code is arranged

Directories are named after layers, not after meter brands. A vendor is a file or a leaf inside a layer.

```
include/gw/            public API, the only headers a caller includes
src/core/          L4  brand-neutral: registry, binding, dispatch
src/drivers/       L3  adapters, one per brand
src/profiles/      L2  what a vendor's data items mean
src/protocols/     L1  wire framing, reusable across vendors
apps/                  demo program
tests/                 the 39 captures and the assertions
```

Includes only ever point downward. L1 knows nothing about L2, and L2 knows nothing about L3.

The split matters because the two halves have very different confidence levels. DL/T 645 framing is a published standard and it is settled, so it lives in `src/protocols/dlt645/` where any brand using that standard can share it. What the data items mean on this particular meter is reverse-engineered guesswork that a single new capture could overturn, so it lives in `src/profiles/sanxing/` where it can be rewritten without touching code that already works.

Symbol prefixes follow the same line. `DLT645_` is standard framing. `SANXING_` is vendor meaning. That distinction is load-bearing, for the reason in the next section.

## Adding another meter brand

A brand appears to the rest of the system as one `gw_driver_t` object holding two function pointers and a spare:

```c
bool        identify(const uint8_t *buf, size_t len);
gw_result_t decode  (const uint8_t *buf, size_t len,
                     gw_reading_t *out, size_t max, size_t *count);
gw_result_t encode  (int cmd, const void *args,
                     uint8_t *out, size_t max, size_t *len);   /* may be NULL */
```

`identify` runs once, at bind time, and should return true only for a complete frame of your protocol with a valid checksum. `decode` may write several readings from one frame; respect `max` and return `GW_ERR_OVERFLOW` rather than writing past it. `buf` is read-only in both, so copy into your own storage if you need to transform bytes. `encode` is the unbuilt command path and is NULL for every driver today, which the core checks before calling.

The steps:

1. Add the brand to `gw_protocol_t` in `include/gw/gw_reading.h`.
2. If the wire format is new, add `src/protocols/<family>/`. If an existing protocol fits, reuse it, since that is the point of keeping framing separate from meaning.
3. Add `src/profiles/<vendor>/` for what that meter's data items mean.
4. Write `src/drivers/<vendor>_driver.c` implementing the functions above.
5. Declare the object in `include/gw/gw_registry.h` and add it to the registry array in `src/core/gw_session.c`.
6. Add the new sources to `LIB_SRC` in the `Makefile`.

Only the last three touch shared files, and each is a single line. `src/drivers/conlog_driver.c` is a registered but inert example: it is selectable, its probe matches nothing, and its decode reports `GW_ERR_UNSUPPORTED` until captures exist for it.

## Do not reuse a stock DL/T 645 data-identifier table

The frame layer is ordinary DL/T 645 and any reference implementation's framing will work. The data identifiers are a different story, and this is the trap worth knowing about before you reuse anything.

Sanxing's identifiers collide with the standard's. Same code, different meaning, and often a different length. `C324` is a 14-byte text container here; a standard table reads it as a 3-byte date, decodes it happily, and returns success. You do not get an error. You get a wrong answer that looks like a right one.

The period numbering diverges too. Real DL/T 645-1997 steps by 4 between billing periods; this meter steps by 1. And energy values are binary 32-bit little-endian rather than the BCD the standard specifies, so a BCD routine reads the real 8937.39 kWh as 140331.

Take the framing from a reference library. Never take its register table.

## What the frames look like

```
68  [6-byte address]  68  C  L  [L data bytes]  CS  16
```

Every data byte is transmitted with `0x33` added to it. The checksum is the low 8 bits of the sum from the first `0x68` through the last data byte. It held for all 78 captured frames.

The part that catches people out is finding frame boundaries. You cannot scan for `0x68` and `0x16`, because the mask puts both inside the data field. ASCII `5` is `0x35`, which transmits as `0x68`. One capture contains four `0x68` bytes and only two of them are real delimiters. The working method is to compute positions from the length byte and then check that the expected bytes are there, which is what `dlt645_parse_frame` does, bounds-checking `L` before trusting it.

After the two DI bytes, a reply carries a 3-digit ASCII code, a status byte, a 2-byte descriptor naming the data item that answered, then the value up to a 7-byte marker. Long replies end with 8 more bytes holding the remaining credit and the lifetime energy, both 32-bit little-endian and both divided by 100. Read those by counting back from the end rather than searching for their values, because they change as soon as the customer draws power.

Four bytes immediately after the marker change on every single reply, including three consecutive reads of the same register. Seventeen of those 32 bits stay fixed for a session and the rest vary, which matches a session tag combined with a counter and a message authentication code. They carry no reading data and should be skipped.

## Known gaps

Codes 050 and 115 (active power) and 800 through 805 (maximum demand timestamps) decode to raw bytes. Their containers are located but not understood. Resolving them needs a load test: switch on a known load, re-read, and see which bytes move. The meter has been sitting at zero consumption for every capture so far, which is also why the lifetime energy register reads the same value in all 37 long replies.

Writing to the meter is unproven. The captures show that a token request carries no counter and no MAC, and that the same 20 digits sent twice produce "Accepted" and then "UsEd", so the security lives in the token rather than in the link. That strongly suggests recharging is a formatting job. It has not been tested against hardware, and section 17 of the protocol reference describes the one test that would settle it.

The 39 captures live in `tests/captures.h` as C byte arrays, with the meter's confirmed values asserted in `tests/test_sanxing.c`. Those two files are the record of what the protocol actually does.
