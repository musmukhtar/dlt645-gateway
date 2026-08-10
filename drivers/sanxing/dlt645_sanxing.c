/*======================================================================
 *  dlt645_sanxing.c  --  VENDOR LAYER for the Sanxing CIU-MH03.
 *
 *  Turns a parsed DL/T 645 payload into a DLT645_Reading: which register
 *  answered (descriptor), the value between the descriptor and the marker,
 *  and the trailer (balance + total energy) at the payload end.
 *
 *  All rules proven in PROTOCOL_REFERENCE.md sections 5-12.
 *====================================================================*/

#include "dlt645_sanxing.h"
#include <string.h>

/* The value-field terminator: 11 C3 40 23 30 00 1A (unmasked). */
static const uint8_t DLT645_VALUE_MARKER[DLT645_VALUE_MARKER_LEN] = {
    0x11, 0xC3, 0x40, 0x23, 0x30, 0x00, 0x1A
};

/*----------------------------------------------------------------------
 *  Register table.  One row per known register.
 *  `code` is NULL except for the two shared container descriptors
 *  (C324, C321), where the 3-digit code disambiguates.
 *  Divisor 1 + BYTES/RAW means "located but not yet scaled".
 *--------------------------------------------------------------------*/
static const DLT645_Register g_registers[] = {
  /* descriptor,                       code,  label,                 encoding,            div,  unit,  quantity */
  { DLT645_DESCRIPTOR_VOLTAGE_LINE_A,   NULL, "Voltage L-A",        DLT645_ENC_U16LE,     10,  "V",   GW_Q_VOLTAGE       },
  { DLT645_DESCRIPTOR_CURRENT_LINE_A,   NULL, "Current L-A",        DLT645_ENC_U32LE,    100,  "A",   GW_Q_CURRENT       },
  { DLT645_DESCRIPTOR_POWER_FACTOR,     NULL, "Power factor",       DLT645_ENC_U16LE,   1000,  "",    GW_Q_POWER_FACTOR  },
  { DLT645_DESCRIPTOR_FREQUENCY,        NULL, "Frequency",          DLT645_ENC_U16LE,    100,  "Hz",  GW_Q_FREQUENCY     },
  { DLT645_DESCRIPTOR_DATE,             NULL, "Date",               DLT645_ENC_BCD_DATE,   1,  "",    GW_Q_DATE          },
  { DLT645_DESCRIPTOR_TIME,             NULL, "Time",               DLT645_ENC_BCD_TIME,   1,  "",    GW_Q_TIME          },
  { DLT645_DESCRIPTOR_ENERGY_TOTAL,     NULL, "Total energy",       DLT645_ENC_U32LE,    100,  "kWh", GW_Q_ENERGY_TOTAL  },
  { DLT645_DESCRIPTOR_ENERGY_MONTH1,    NULL, "Energy month -1",    DLT645_ENC_U32LE,    100,  "kWh", GW_Q_ENERGY_PERIOD },
  { DLT645_DESCRIPTOR_ENERGY_MONTH2,    NULL, "Energy month -2",    DLT645_ENC_U32LE,    100,  "kWh", GW_Q_ENERGY_PERIOD },
  { DLT645_DESCRIPTOR_ENERGY_MONTH3,    NULL, "Energy month -3",    DLT645_ENC_U32LE,    100,  "kWh", GW_Q_ENERGY_PERIOD },
  { DLT645_DESCRIPTOR_ENERGY_DAY1,      NULL, "Energy day -1",      DLT645_ENC_U32LE,    100,  "kWh", GW_Q_ENERGY_PERIOD },
  { DLT645_DESCRIPTOR_ENERGY_DAY2,      NULL, "Energy day -2",      DLT645_ENC_U32LE,    100,  "kWh", GW_Q_ENERGY_PERIOD },
  { DLT645_DESCRIPTOR_ENERGY_DAY3,      NULL, "Energy day -3",      DLT645_ENC_U32LE,    100,  "kWh", GW_Q_ENERGY_PERIOD },
  { DLT645_DESCRIPTOR_MAXDEM_CURRENT,   NULL, "Max demand",         DLT645_ENC_U32LE,    100,  "kW",  GW_Q_MAX_DEMAND    },
  { DLT645_DESCRIPTOR_MAXDEM_MONTH1,    NULL, "Max demand -1",      DLT645_ENC_U32LE,    100,  "kW",  GW_Q_MAX_DEMAND    },
  { DLT645_DESCRIPTOR_MAXDEM_MONTH2,    NULL, "Max demand -2",      DLT645_ENC_U32LE,    100,  "kW",  GW_Q_MAX_DEMAND    },
  { DLT645_DESCRIPTOR_REMINDER_CREDIT,  NULL, "Credit",             DLT645_ENC_U32LE,    100,  "kWh", GW_Q_CREDIT        },
  { DLT645_DESCRIPTOR_SUPPLY_DAYS,      NULL, "Supply days",        DLT645_ENC_U32LE,      1,  "days",GW_Q_SUPPLY_DAYS   },
  { DLT645_DESCRIPTOR_MAX_DEMAND,       NULL, "Max demand limit",   DLT645_ENC_U32LE,      1,  "",    GW_Q_MAX_DEMAND    },
  { DLT645_DESCRIPTOR_TARIFF_INDEX,     NULL, "Tariff index",       DLT645_ENC_BYTES,      1,  "",    GW_Q_TARIFF        },
  { DLT645_DESCRIPTOR_LCD_TEST,         NULL, "LCD test",           DLT645_ENC_BYTES,      1,  "",    GW_Q_RAW           },
  { DLT645_DESCRIPTOR_SUPPLY_GROUP,     NULL, "Supply group code",  DLT645_ENC_BYTES,      1,  "",    GW_Q_RAW           },
  { DLT645_DESCRIPTOR_METER_NUMBER,     NULL, "Meter number",       DLT645_ENC_BYTES,      1,  "",    GW_Q_METER_ID      },
  { DLT645_DESCRIPTOR_TOKEN_AMOUNT,     NULL, "Token amount",       DLT645_ENC_U32LE,    100,  "kWh", GW_Q_CREDIT        },
  { DLT645_DESCRIPTOR_TOKEN_RESULT,     NULL, "Token result",       DLT645_ENC_TEXT,       1,  "",    GW_Q_TOKEN_RESULT  },
  /* container C324, disambiguated by 3-digit code */
  { DLT645_DESCRIPTOR_TEXT_BLOCK,      "185", "Over-V threshold",   DLT645_ENC_ASCII_NUM,  10,  "V",   GW_Q_THRESHOLD     },
  { DLT645_DESCRIPTOR_TEXT_BLOCK,      "186", "Under-V threshold",  DLT645_ENC_ASCII_NUM,  10,  "V",   GW_Q_THRESHOLD     },
  { DLT645_DESCRIPTOR_TEXT_BLOCK,      "050", "Active power",       DLT645_ENC_BYTES,       1,  "",    GW_Q_RAW           },
  { DLT645_DESCRIPTOR_TEXT_BLOCK,      "115", "Active power",       DLT645_ENC_BYTES,       1,  "",    GW_Q_RAW           },
  /* container C321, disambiguated by 3-digit code */
  { DLT645_DESCRIPTOR_RECORD_BLOCK,   "801", "Max demand time",    DLT645_ENC_BYTES,       1,  "",    GW_Q_RAW           },
  { DLT645_DESCRIPTOR_RECORD_BLOCK,   "803", "Max demand time -1", DLT645_ENC_BYTES,       1,  "",    GW_Q_RAW           },
  { DLT645_DESCRIPTOR_RECORD_BLOCK,   "805", "Max demand time -2", DLT645_ENC_BYTES,       1,  "",    GW_Q_RAW           },
};

#define DLT645_REGISTER_COUNT (sizeof(g_registers) / sizeof(g_registers[0]))

const DLT645_Register *dlt645_lookup(uint16_t descriptor, const char *code)
{
    for (size_t i = 0; i < DLT645_REGISTER_COUNT; i++) {
        const DLT645_Register *r = &g_registers[i];
        if (r->descriptor != descriptor) {
            continue;
        }
        if (r->code == NULL) {
            return r;   /* match on descriptor alone */
        }
        if (code != NULL && strncmp(r->code, code, DLT645_CODE_LEN) == 0) {
            return r;   /* container: descriptor + code both match */
        }
    }
    return NULL;
}

/* ---- small helpers ---- */

static uint32_t rd_u32le(const uint8_t *p, uint8_t n)
{
    uint32_t v = 0;
    for (uint8_t i = 0; i < n && i < 4; i++) {
        v |= (uint32_t)p[i] << (8 * i);
    }
    return v;
}

static uint8_t bcd(uint8_t b)
{
    return (uint8_t)((b >> 4) * 10 + (b & 0x0F));
}

/* Collect the printable-ASCII run that follows the last 0xFF byte in the
   value field.  This isolates "UsEd" / "rEJECt" from their FF FF prefix,
   and "2708" from the FF padding of the threshold container. */
static void tail_ascii(const uint8_t *raw, uint8_t len, char *out, size_t out_max)
{
    size_t start = 0;
    for (uint8_t i = 0; i < len; i++) {
        if (raw[i] == 0xFF) {
            start = (size_t)i + 1;
        }
    }
    size_t j = 0;
    for (size_t i = start; i < len && j + 1 < out_max; i++) {
        uint8_t c = raw[i];
        if (c >= 0x20 && c <= 0x7E) {
            out[j++] = (char)c;
        } else if (j > 0) {
            break;   /* stop at first non-printable after text begins */
        }
    }
    out[j] = '\0';
}

static int32_t ascii_to_int(const char *s)
{
    int32_t v = 0;
    for (; *s; s++) {
        if (*s >= '0' && *s <= '9') {
            v = v * 10 + (*s - '0');
        }
    }
    return v;
}

/*----------------------------------------------------------------------
 *  Decode one parsed frame into a reading.
 *--------------------------------------------------------------------*/
DLT645_Result dlt645_decode_reading(const DLT645_Frame_Info *frame,
                                    DLT645_Reading *out)
{
    if (frame == NULL || out == NULL) {
        return DLT645_ERR_NULL;
    }
    memset(out, 0, sizeof(*out));

    /* A request carries only the DI + 3-digit code, no status/descriptor/
       value.  Refuse it explicitly rather than mis-reading it as a reply. */
    if (!frame->is_response) {
        return DLT645_ERR_NOT_RESPONSE;
    }

    const uint8_t *p   = frame->payload;
    uint8_t        len = frame->payload_len;
    uint8_t        value_start;

    /* --- locate status, descriptor and the value, per family --- */
    if (frame->di == DLT645_HASH_DATA_IDENTIFIER) {          /* E002 reading */
        if (len < DLT645_E002_OFF_VALUE) {
            return DLT645_ERR_SHORT_PAYLOAD;
        }
        out->code[0] = (char)p[DLT645_E002_OFF_CODE + 0];
        out->code[1] = (char)p[DLT645_E002_OFF_CODE + 1];
        out->code[2] = (char)p[DLT645_E002_OFF_CODE + 2];
        out->code[3] = '\0';
        out->meter_status = p[DLT645_E002_OFF_STATUS];
        out->descriptor   = (uint16_t)((p[DLT645_E002_OFF_DESCR + 1] << 8) |
                                        p[DLT645_E002_OFF_DESCR + 0]);
        value_start = DLT645_E002_OFF_VALUE;
    } else if (frame->di == DLT645_TOKEN_DATA_IDENTIFIER) {  /* E001 token */
        if (len < DLT645_E001_OFF_VALUE) {
            return DLT645_ERR_SHORT_PAYLOAD;
        }
        out->code[0] = '\0';
        out->meter_status = p[DLT645_E001_OFF_STATUS];
        out->descriptor   = (uint16_t)((p[DLT645_E001_OFF_DESCR + 1] << 8) |
                                        p[DLT645_E001_OFF_DESCR + 0]);
        value_start = DLT645_E001_OFF_VALUE;
    } else {
        return DLT645_ERR_UNKNOWN_DI;
    }

    /* --- find the value marker (guarded search) --- */
    int marker_at = -1;
    if (len >= DLT645_VALUE_MARKER_LEN) {
        for (uint8_t i = value_start; i + DLT645_VALUE_MARKER_LEN <= len; i++) {
            if (memcmp(&p[i], DLT645_VALUE_MARKER, DLT645_VALUE_MARKER_LEN) == 0) {
                marker_at = i;
                break;
            }
        }
    }

    uint8_t value_end = (marker_at >= 0) ? (uint8_t)marker_at : len;
    out->raw     = &p[value_start];
    out->raw_len = (value_end > value_start) ? (uint8_t)(value_end - value_start) : 0;

    /* --- trailer: only when the marker is present (long replies) --- */
    if (marker_at >= 0 && len >= DLT645_TRAILER_LEN) {
        const uint8_t *t = &p[len - DLT645_TRAILER_LEN];
        out->balance      = rd_u32le(t, 4);
        out->total_energy = rd_u32le(t + 4, 4);
        out->has_trailer  = true;
    }

    /* --- table lookup + value decode --- */
    const char *code = (frame->di == DLT645_HASH_DATA_IDENTIFIER) ? out->code : NULL;
    out->reg = dlt645_lookup(out->descriptor, code);
    out->divisor = out->reg ? out->reg->divisor : 1;

    DLT645_Encoding enc = out->reg ? out->reg->encoding : DLT645_ENC_BYTES;

    switch (enc) {
    case DLT645_ENC_U16LE:
        out->value = (int32_t)rd_u32le(out->raw, out->raw_len < 2 ? out->raw_len : 2);
        out->has_value = true;
        break;
    case DLT645_ENC_U32LE:
        out->value = (int32_t)rd_u32le(out->raw, out->raw_len < 4 ? out->raw_len : 4);
        out->has_value = true;
        break;
    case DLT645_ENC_BCD_DATE:
        if (out->raw_len >= 4) {
            out->datetime.weekday = bcd(out->raw[0]);
            out->datetime.year    = bcd(out->raw[1]);
            out->datetime.month   = bcd(out->raw[2]);
            out->datetime.day     = bcd(out->raw[3]);
            out->has_datetime = true;
        }
        break;
    case DLT645_ENC_BCD_TIME:
        if (out->raw_len >= 3) {
            out->datetime.hour   = bcd(out->raw[0]);
            out->datetime.minute = bcd(out->raw[1]);
            out->datetime.second = bcd(out->raw[2]);
            out->has_datetime = true;
        }
        break;
    case DLT645_ENC_ASCII_NUM: {
        char tmp[GW_READING_TEXT_MAX];
        tail_ascii(out->raw, out->raw_len, tmp, sizeof(tmp));
        out->value = ascii_to_int(tmp);
        out->has_value = true;
        break;
    }
    case DLT645_ENC_TEXT:
        tail_ascii(out->raw, out->raw_len, out->text, sizeof(out->text));
        out->has_text = (out->text[0] != '\0');
        break;
    case DLT645_ENC_BYTES:
    default:
        /* located, meaning still raw; leave raw/raw_len for the caller */
        break;
    }

    return DLT645_OK;
}
