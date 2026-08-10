#ifndef DLT645_SANXING_H
#define DLT645_SANXING_H

#include <stdint.h>
#include <stdbool.h>
#include "dlt645.h"
#include "gw_reading.h"   /* for gw_quantity_t on each register row */

/*======================================================================
 *  dlt645_sanxing.h  --  VENDOR LAYER (Sanxing-specific, a hypothesis).
 *
 *  Descriptors, value encodings, scaling, the trailer and the containers.
 *  All of this is reverse-engineered from captures (PROTOCOL_REFERENCE.md)
 *  and can change when a new capture arrives.  The frame layer (dlt645.h)
 *  never depends on anything here.
 *====================================================================*/

/* ---- descriptor = the meter register reported back, (DI1<<8)|DI0 ---- */
/*      class nibble: 9x energy, Ax max-demand, C4 instantaneous,
 *      C1 clock, C0/C3 parameters & containers (vendor numbering).      */
/* instantaneous (C4) */
#define DLT645_DESCRIPTOR_VOLTAGE_LINE_A   0xC400
#define DLT645_DESCRIPTOR_CURRENT_LINE_A   0xC410
#define DLT645_DESCRIPTOR_POWER_FACTOR     0xC451
#define DLT645_DESCRIPTOR_FREQUENCY        0xC470
/* clock / limits (C1) */
#define DLT645_DESCRIPTOR_MAX_DEMAND       0xC13C
#define DLT645_DESCRIPTOR_DATE             0xC195
#define DLT645_DESCRIPTOR_TIME             0xC180
/* energy (9x): DI1 low nibble = period (0 current, 1..3 back) */
#define DLT645_DESCRIPTOR_ENERGY_TOTAL     0x9010
#define DLT645_DESCRIPTOR_ENERGY_MONTH1    0x9119
#define DLT645_DESCRIPTOR_ENERGY_MONTH2    0x9219
#define DLT645_DESCRIPTOR_ENERGY_MONTH3    0x9319
#define DLT645_DESCRIPTOR_ENERGY_DAY1      0x911A
#define DLT645_DESCRIPTOR_ENERGY_DAY2      0x921A
#define DLT645_DESCRIPTOR_ENERGY_DAY3      0x931A
/* max-demand records (Ax) */
#define DLT645_DESCRIPTOR_MAXDEM_CURRENT   0xA010
#define DLT645_DESCRIPTOR_MAXDEM_MONTH1    0xA110
#define DLT645_DESCRIPTOR_MAXDEM_MONTH2    0xA210
/* parameters / misc (C0/C3) */
#define DLT645_DESCRIPTOR_LCD_TEST         0xC330
#define DLT645_DESCRIPTOR_TARIFF_INDEX     0xC377
#define DLT645_DESCRIPTOR_REMINDER_CREDIT  0xC362
#define DLT645_DESCRIPTOR_SUPPLY_DAYS      0xC367
#define DLT645_DESCRIPTOR_METER_NUMBER     0xC004
#define DLT645_DESCRIPTOR_SUPPLY_GROUP     0xC00A
/* tokens (E001) */
#define DLT645_DESCRIPTOR_TOKEN_AMOUNT     0xC375
#define DLT645_DESCRIPTOR_TOKEN_RESULT     0xC320
/* containers (one DI shared by several 3-digit codes) */
#define DLT645_DESCRIPTOR_TEXT_BLOCK       0xC324  /* 14-byte text   */
#define DLT645_DESCRIPTOR_RECORD_BLOCK     0xC321  /* 21-byte record */

/* ---- payload offsets, counted AFTER the 2 DI bytes are removed ---- */
/* reading (E002):  "004" 50 [10 90] value... */
#define DLT645_E002_OFF_CODE      0
#define DLT645_E002_OFF_STATUS    3
#define DLT645_E002_OFF_DESCR     4
#define DLT645_E002_OFF_VALUE     6
/* token (E001):    00 50 00 [75 C3] value... */
#define DLT645_E001_OFF_STATUS    1
#define DLT645_E001_OFF_DESCR     3
#define DLT645_E001_OFF_VALUE     5
#define DLT645_CODE_LEN           3

/* ---- markers / trailer ---- */
#define DLT645_VALUE_MARKER_LEN   7   /* 11 C3 40 23 30 00 1A */
#define DLT645_TRAILER_LEN        8   /* [balance u32][total energy u32] */

/* ---- meter status byte (real wire values) ---- */
typedef enum {
    DLT645_METER_OK     = 0x50,  /* reading ok / token accepted */
    DLT645_METER_ERROR  = 0x51,  /* reading error               */
    DLT645_METER_FAILED = 0x0B   /* token failed (see text)     */
} DLT645_Meter_Status;

/* ---- how to read the value bytes ---- */
typedef enum {
    DLT645_ENC_U16LE,      /* 56 09       -> 2390    voltage, PF, freq */
    DLT645_ENC_U32LE,      /* 53 3F 00 00 -> 16211   energy, credit    */
    DLT645_ENC_BCD_DATE,   /* 02 26 08 04 -> 2026-08-04                */
    DLT645_ENC_BCD_TIME,   /* 14 24 31    -> 14:24:31                  */
    DLT645_ENC_ASCII_NUM,  /* "2708"      -> 2708    C324 container    */
    DLT645_ENC_TEXT,       /* "rEJECt"    -> text, no number           */
    DLT645_ENC_BYTES       /* raw: meter number, SGC, C321 record      */
} DLT645_Encoding;

/* ---- one register table row (the whole vendor layer is an array) ---- */
typedef struct {
    uint16_t         descriptor;  /* 0xC400 -- THE key                  */
    const char      *code;        /* NULL, or "185" for shared C324/C321 */
    const char      *label;       /* "Voltage L-A"                      */
    DLT645_Encoding  encoding;
    uint16_t         divisor;     /* real value = value / divisor       */
    const char      *unit;        /* "V", "kWh", ""                     */
    gw_quantity_t    quantity;    /* brand-neutral mapping              */
} DLT645_Register;

/* ---- BCD date / time ---- */
typedef struct {
    uint8_t weekday, year, month, day;   /* date registers */
    uint8_t hour, minute, second;        /* time registers */
} DLT645_DateTime;

/* ---- the decoded reading ---- */
typedef struct {
    uint16_t     descriptor;
    char         code[DLT645_CODE_LEN + 1];  /* "111", or "" for tokens */
    uint8_t      meter_status;               /* see DLT645_Meter_Status */
    const DLT645_Register *reg;              /* matched row, or NULL     */

    bool         has_value;
    int32_t      value;        /* STILL SCALED (2390, not 239.0)        */
    uint16_t     divisor;

    bool         has_text;
    char         text[GW_READING_TEXT_MAX];

    bool             has_datetime;
    DLT645_DateTime  datetime;

    const uint8_t *raw;        /* value bytes, into the caller scratch  */
    uint8_t        raw_len;

    bool         has_trailer;
    uint32_t     balance;      /* /100 -> kWh */
    uint32_t     total_energy; /* /100 -> kWh */
} DLT645_Reading;

/*----------------------------------------------------------------------
 *  Table lookup.  code may be NULL; it is consulted only for the shared
 *  container descriptors C324 and C321.  Returns NULL if not found.
 *--------------------------------------------------------------------*/
const DLT645_Register *dlt645_lookup(uint16_t descriptor, const char *code);

/*----------------------------------------------------------------------
 *  Decode a parsed frame's payload into a reading: descriptor, value,
 *  trailer.  Handles both E002 (readings) and E001 (tokens).
 *--------------------------------------------------------------------*/
DLT645_Result dlt645_decode_reading(const DLT645_Frame_Info *frame,
                                    DLT645_Reading *out);

#endif /* DLT645_SANXING_H */
