#ifndef SANXING_PROFILE_H
#define SANXING_PROFILE_H

#include <stdint.h>
#include <stdbool.h>
#include "dlt645_frame.h"
#include "gw/gw_reading.h"   /* for gw_quantity_t on each DI row */

/* What the data items mean on a Sanxing CIU-MH03: which data identifiers
   exist, how each value is encoded, its scale and its name.  Reverse
   engineered from captures, so a new capture can overturn any of it.

   WARNING: these DI values are Sanxing's and they collide with standard
   DL/T 645-1997 assignments.  Same code, different meaning, often a
   different length.  C324 and C321 are containers here (14 and 21 bytes);
   a standard table reads them as a 3-byte date and returns success, which
   is a wrong answer rather than an error.  Do not resolve these with a
   stock DL/T 645 table.  Hence the SANXING_ prefix throughout. */

/* Outer DI, vendor-defined 0xE0xx user range.  Transmitted low byte first,
   so E002 appears as 02 E0 once unmasked. */
#define SANXING_DI_TOKEN     0xE001  /* token / recharge operations */
#define SANXING_DI_READING   0xE002  /* meter readings, by 3-digit code */

/* Descriptor: the DI the meter echoes back, (DI1<<8)|DI0.  Not the same as
   the outer DI above, which names the command family.
   Class nibble: 9x energy, Ax max-demand, C4 instantaneous, C1 clock,
   C0/C3 parameters and containers. */
/* instantaneous (C4) */
#define SANXING_DESCRIPTOR_VOLTAGE_LINE_A   0xC400
#define SANXING_DESCRIPTOR_CURRENT_LINE_A   0xC410
#define SANXING_DESCRIPTOR_POWER_FACTOR     0xC451
#define SANXING_DESCRIPTOR_FREQUENCY        0xC470
/* clock / limits (C1) */
#define SANXING_DESCRIPTOR_MAX_DEMAND       0xC13C
#define SANXING_DESCRIPTOR_DATE             0xC195
#define SANXING_DESCRIPTOR_TIME             0xC180
/* energy (9x): DI1 low nibble = period (0 current, 1..3 back) */
#define SANXING_DESCRIPTOR_ENERGY_TOTAL     0x9010
#define SANXING_DESCRIPTOR_ENERGY_MONTH1    0x9119
#define SANXING_DESCRIPTOR_ENERGY_MONTH2    0x9219
#define SANXING_DESCRIPTOR_ENERGY_MONTH3    0x9319
#define SANXING_DESCRIPTOR_ENERGY_DAY1      0x911A
#define SANXING_DESCRIPTOR_ENERGY_DAY2      0x921A
#define SANXING_DESCRIPTOR_ENERGY_DAY3      0x931A
/* max-demand records (Ax) */
#define SANXING_DESCRIPTOR_MAXDEM_CURRENT   0xA010
#define SANXING_DESCRIPTOR_MAXDEM_MONTH1    0xA110
#define SANXING_DESCRIPTOR_MAXDEM_MONTH2    0xA210
/* parameters / misc (C0/C3) */
#define SANXING_DESCRIPTOR_LCD_TEST         0xC330
#define SANXING_DESCRIPTOR_TARIFF_INDEX     0xC377
#define SANXING_DESCRIPTOR_REMINDER_CREDIT  0xC362
#define SANXING_DESCRIPTOR_SUPPLY_DAYS      0xC367
#define SANXING_DESCRIPTOR_METER_NUMBER     0xC004
#define SANXING_DESCRIPTOR_SUPPLY_GROUP     0xC00A
/* tokens (E001) */
#define SANXING_DESCRIPTOR_TOKEN_AMOUNT     0xC375
#define SANXING_DESCRIPTOR_TOKEN_RESULT     0xC320
/* containers: one DI shared by several 3-digit codes */
#define SANXING_DESCRIPTOR_TEXT_BLOCK       0xC324  /* 14-byte text   */
#define SANXING_DESCRIPTOR_RECORD_BLOCK     0xC321  /* 21-byte record */

/* Payload offsets, counted after the 2 DI bytes are removed. */
/* reading (E002):  "004" 50 [10 90] value... */
#define SANXING_E002_OFF_CODE      0
#define SANXING_E002_OFF_STATUS    3
#define SANXING_E002_OFF_DESCR     4
#define SANXING_E002_OFF_VALUE     6
/* token (E001):    00 50 00 [75 C3] value... */
#define SANXING_E001_OFF_STATUS    1
#define SANXING_E001_OFF_DESCR     3
#define SANXING_E001_OFF_VALUE     5
#define SANXING_CODE_LEN           3

/* Markers and trailer. */
#define SANXING_VALUE_MARKER_LEN   7   /* 11 C3 40 23 30 00 1A */
#define SANXING_TRAILER_LEN        8   /* [balance u32][total energy u32] */

/* Meter status byte, real wire values. */
typedef enum {
    SANXING_METER_OK     = 0x50,  /* reading ok / token accepted */
    SANXING_METER_ERROR  = 0x51,  /* reading error               */
    SANXING_METER_FAILED = 0x0B   /* token failed (see text)     */
} Sanxing_Meter_Status;

/* Kept separate from DLT645_Result: a frame can be well formed and still
   carry a payload this layer cannot read. */
typedef enum {
    SANXING_OK = 0,
    SANXING_ERR_NULL,            /* NULL argument                      */
    SANXING_ERR_NOT_RESPONSE,    /* a request frame, not a reply       */
    SANXING_ERR_UNKNOWN_DI,      /* outer DI is not E001 and not E002  */
    SANXING_ERR_SHORT_PAYLOAD    /* too short for code+status+descr    */
} Sanxing_Result;

/* How to read the value bytes. */
typedef enum {
    SANXING_ENC_U16LE,      /* 56 09       -> 2390    voltage, PF, freq */
    SANXING_ENC_U32LE,      /* 53 3F 00 00 -> 16211   energy, credit    */
    SANXING_ENC_BCD_DATE,   /* 02 26 08 04 -> 2026-08-04                */
    SANXING_ENC_BCD_TIME,   /* 14 24 31    -> 14:24:31                  */
    SANXING_ENC_ASCII_NUM,  /* "2708"      -> 2708    C324 container    */
    SANXING_ENC_TEXT,       /* "rEJECt"    -> text, no number           */
    SANXING_ENC_BYTES       /* raw: meter number, SGC, C321 record      */
} Sanxing_Encoding;

/* One table row. */
typedef struct {
    uint16_t          descriptor;  /* 0xC400 -- THE key                   */
    const char       *code;        /* NULL, or "185" for shared C324/C321 */
    const char       *label;       /* "Voltage L-A"                       */
    Sanxing_Encoding  encoding;
    uint16_t          divisor;     /* real value = value / divisor        */
    const char       *unit;        /* "V", "kWh", ""                      */
    gw_quantity_t     quantity;    /* brand-neutral mapping               */
} Sanxing_DI_Desc;

/* BCD date and time. */
typedef struct {
    uint8_t weekday, year, month, day;   /* date fields */
    uint8_t hour, minute, second;        /* time fields */
} Sanxing_DateTime;

/* The decoded reading. */
typedef struct {
    uint16_t     descriptor;                  /* the DI echoed back      */
    char         code[SANXING_CODE_LEN + 1];  /* "111", or "" for tokens */
    uint8_t      meter_status;                /* see Sanxing_Meter_Status */
    const Sanxing_DI_Desc *desc;              /* matched row, or NULL     */

    bool         has_value;
    int32_t      value;        /* STILL SCALED (2390, not 239.0)        */
    uint16_t     divisor;

    bool         has_text;
    char         text[GW_READING_TEXT_MAX];

    bool              has_datetime;
    Sanxing_DateTime  datetime;

    const uint8_t *raw;        /* value bytes, into the frame's data buf */
    uint8_t        raw_len;

    bool         has_trailer;
    uint32_t     balance;      /* /100 -> kWh */
    uint32_t     total_energy; /* /100 -> kWh */
} Sanxing_Reading;

/* code may be NULL; it is only consulted for the shared container DIs C324
   and C321.  Returns NULL if not found.  The returned row points into a
   static table and stays valid for the life of the program, unlike
   Sanxing_Reading.raw which dies at the next parse. */
const Sanxing_DI_Desc *sanxing_lookup_descriptor(uint16_t descriptor,
                                                 const char *code);

/*----------------------------------------------------------------------
 *  Decode a parsed frame's payload into a reading: descriptor, value,
 *  trailer.
 *  Handles both E002 (readings) and E001 (tokens).
 *--------------------------------------------------------------------*/
Sanxing_Result sanxing_decode_reading(const DLT645_Frame_Info *frame,
                                      Sanxing_Reading *out);

#endif /* SANXING_PROFILE_H */
