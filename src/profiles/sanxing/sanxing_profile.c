/* Turns a parsed DL/T 645 payload into a Sanxing_Reading: which DI
   answered, the value between the descriptor and the marker, and the
   trailer (balance and total energy) at the end of the payload. */

#include "sanxing_profile.h"
#include <string.h>

/* Value-field terminator, unmasked. */
static const uint8_t SANXING_VALUE_MARKER[SANXING_VALUE_MARKER_LEN] = {
    0x11, 0xC3, 0x40, 0x23, 0x30, 0x00, 0x1A
};

/* One row per known DI.  `code` is NULL except for the shared containers
   C324 and C321, where the 3-digit code disambiguates.  Divisor 1 with
   ENC_BYTES means located but not yet understood. */
static const Sanxing_DI_Desc g_di_table[] = {
  /* descriptor,                       code,  label,                 encoding,            div,  unit,  quantity */
  { SANXING_DESCRIPTOR_VOLTAGE_LINE_A,   NULL, "Voltage L-A",        SANXING_ENC_U16LE,     10,  "V",   GW_Q_VOLTAGE       },
  { SANXING_DESCRIPTOR_CURRENT_LINE_A,   NULL, "Current L-A",        SANXING_ENC_U32LE,    100,  "A",   GW_Q_CURRENT       },
  { SANXING_DESCRIPTOR_POWER_FACTOR,     NULL, "Power factor",       SANXING_ENC_U16LE,   1000,  "",    GW_Q_POWER_FACTOR  },
  { SANXING_DESCRIPTOR_FREQUENCY,        NULL, "Frequency",          SANXING_ENC_U16LE,    100,  "Hz",  GW_Q_FREQUENCY     },
  { SANXING_DESCRIPTOR_DATE,             NULL, "Date",               SANXING_ENC_BCD_DATE,   1,  "",    GW_Q_DATE          },
  { SANXING_DESCRIPTOR_TIME,             NULL, "Time",               SANXING_ENC_BCD_TIME,   1,  "",    GW_Q_TIME          },
  { SANXING_DESCRIPTOR_ENERGY_TOTAL,     NULL, "Total energy",       SANXING_ENC_U32LE,    100,  "kWh", GW_Q_ENERGY_TOTAL  },
  { SANXING_DESCRIPTOR_ENERGY_MONTH1,    NULL, "Energy month -1",    SANXING_ENC_U32LE,    100,  "kWh", GW_Q_ENERGY_PERIOD },
  { SANXING_DESCRIPTOR_ENERGY_MONTH2,    NULL, "Energy month -2",    SANXING_ENC_U32LE,    100,  "kWh", GW_Q_ENERGY_PERIOD },
  { SANXING_DESCRIPTOR_ENERGY_MONTH3,    NULL, "Energy month -3",    SANXING_ENC_U32LE,    100,  "kWh", GW_Q_ENERGY_PERIOD },
  { SANXING_DESCRIPTOR_ENERGY_DAY1,      NULL, "Energy day -1",      SANXING_ENC_U32LE,    100,  "kWh", GW_Q_ENERGY_PERIOD },
  { SANXING_DESCRIPTOR_ENERGY_DAY2,      NULL, "Energy day -2",      SANXING_ENC_U32LE,    100,  "kWh", GW_Q_ENERGY_PERIOD },
  { SANXING_DESCRIPTOR_ENERGY_DAY3,      NULL, "Energy day -3",      SANXING_ENC_U32LE,    100,  "kWh", GW_Q_ENERGY_PERIOD },
  { SANXING_DESCRIPTOR_MAXDEM_CURRENT,   NULL, "Max demand",         SANXING_ENC_U32LE,    100,  "kW",  GW_Q_MAX_DEMAND    },
  { SANXING_DESCRIPTOR_MAXDEM_MONTH1,    NULL, "Max demand -1",      SANXING_ENC_U32LE,    100,  "kW",  GW_Q_MAX_DEMAND    },
  { SANXING_DESCRIPTOR_MAXDEM_MONTH2,    NULL, "Max demand -2",      SANXING_ENC_U32LE,    100,  "kW",  GW_Q_MAX_DEMAND    },
  { SANXING_DESCRIPTOR_REMINDER_CREDIT,  NULL, "Credit",             SANXING_ENC_U32LE,    100,  "kWh", GW_Q_CREDIT        },
  { SANXING_DESCRIPTOR_SUPPLY_DAYS,      NULL, "Supply days",        SANXING_ENC_U32LE,      1,  "days",GW_Q_SUPPLY_DAYS   },
  { SANXING_DESCRIPTOR_MAX_DEMAND,       NULL, "Max demand limit",   SANXING_ENC_U32LE,      1,  "",    GW_Q_MAX_DEMAND    },
  { SANXING_DESCRIPTOR_TARIFF_INDEX,     NULL, "Tariff index",       SANXING_ENC_BYTES,      1,  "",    GW_Q_TARIFF        },
  { SANXING_DESCRIPTOR_LCD_TEST,         NULL, "LCD test",           SANXING_ENC_BYTES,      1,  "",    GW_Q_RAW           },
  { SANXING_DESCRIPTOR_SUPPLY_GROUP,     NULL, "Supply group code",  SANXING_ENC_BYTES,      1,  "",    GW_Q_RAW           },
  { SANXING_DESCRIPTOR_METER_NUMBER,     NULL, "Meter number",       SANXING_ENC_BYTES,      1,  "",    GW_Q_METER_ID      },
  { SANXING_DESCRIPTOR_TOKEN_AMOUNT,     NULL, "Token amount",       SANXING_ENC_U32LE,    100,  "kWh", GW_Q_CREDIT        },
  { SANXING_DESCRIPTOR_TOKEN_RESULT,     NULL, "Token result",       SANXING_ENC_TEXT,       1,  "",    GW_Q_TOKEN_RESULT  },
  /* container C324, disambiguated by 3-digit code */
  { SANXING_DESCRIPTOR_TEXT_BLOCK,      "185", "Over-V threshold",   SANXING_ENC_ASCII_NUM,  10,  "V",   GW_Q_THRESHOLD     },
  { SANXING_DESCRIPTOR_TEXT_BLOCK,      "186", "Under-V threshold",  SANXING_ENC_ASCII_NUM,  10,  "V",   GW_Q_THRESHOLD     },
  { SANXING_DESCRIPTOR_TEXT_BLOCK,      "050", "Active power",       SANXING_ENC_BYTES,       1,  "",    GW_Q_RAW           },
  { SANXING_DESCRIPTOR_TEXT_BLOCK,      "115", "Active power",       SANXING_ENC_BYTES,       1,  "",    GW_Q_RAW           },
  /* container C321, disambiguated by 3-digit code */
  { SANXING_DESCRIPTOR_RECORD_BLOCK,   "801", "Max demand time",    SANXING_ENC_BYTES,       1,  "",    GW_Q_RAW           },
  { SANXING_DESCRIPTOR_RECORD_BLOCK,   "803", "Max demand time -1", SANXING_ENC_BYTES,       1,  "",    GW_Q_RAW           },
  { SANXING_DESCRIPTOR_RECORD_BLOCK,   "805", "Max demand time -2", SANXING_ENC_BYTES,       1,  "",    GW_Q_RAW           },
};

#define SANXING_DI_COUNT (sizeof(g_di_table) / sizeof(g_di_table[0]))

const Sanxing_DI_Desc *sanxing_lookup_descriptor(uint16_t descriptor, const char *code)
{
    for (size_t index = 0; index < SANXING_DI_COUNT; index++) {
        const Sanxing_DI_Desc *row = &g_di_table[index];
        if (row->descriptor != descriptor) {
            continue;
        }
        if (row->code == NULL) {
            return row;   /* match on descriptor alone */
        }
        if (code != NULL && strncmp(row->code, code, SANXING_CODE_LEN) == 0) {
            return row;   /* container: descriptor + code both match */
        }
    }
    return NULL;
}

/* ---- small helpers ---- */

/* Read 1..4 bytes as a little-endian unsigned integer. */
static uint32_t read_uint_le(const uint8_t *bytes, uint8_t nbytes)
{
    uint32_t value = 0;
    for (uint8_t pos = 0; pos < nbytes && pos < 4; pos++) {
        value |= (uint32_t)bytes[pos] << (8 * pos);
    }
    return value;
}

static uint8_t bcd(uint8_t byte)
{
    return (uint8_t)((byte >> 4) * 10 + (byte & 0x0F));
}

/* Printable-ASCII run following the last 0xFF byte.  Isolates "UsEd" and
   "rEJECt" from their FF FF prefix, and "2708" from the FF padding. */
static void tail_ascii(const uint8_t *raw, uint8_t raw_len, char *out, size_t out_max)
{
    size_t text_start = 0;
    for (uint8_t pos = 0; pos < raw_len; pos++) {
        if (raw[pos] == 0xFF) {
            text_start = (size_t)pos + 1;
        }
    }
    size_t out_pos = 0;
    for (size_t pos = text_start; pos < raw_len && out_pos + 1 < out_max; pos++) {
        uint8_t ch = raw[pos];
        if (ch >= 0x20 && ch <= 0x7E) {
            out[out_pos++] = (char)ch;
        } else if (out_pos > 0) {
            break;   /* stop at first non-printable after text begins */
        }
    }
    out[out_pos] = '\0';
}

static int32_t ascii_to_int(const char *str)
{
    int32_t value = 0;
    for (; *str; str++) {
        if (*str >= '0' && *str <= '9') {
            value = value * 10 + (*str - '0');
        }
    }
    return value;
}

/* Decode one parsed frame into a reading. */
Sanxing_Result sanxing_decode_reading(const DLT645_Frame_Info *frame,
                                      Sanxing_Reading *out)
{
    if (frame == NULL || out == NULL) {
        return SANXING_ERR_NULL;
    }
    memset(out, 0, sizeof(*out));

    /* A request carries only the DI and 3-digit code, no status or value.
       Refuse it rather than mis-reading it as a reply. */
    if (!frame->is_response) {
        return SANXING_ERR_NOT_RESPONSE;
    }

    const uint8_t *payload     = frame->payload;
    uint8_t        payload_len = frame->payload_len;
    uint8_t        value_start;

    /* locate status, descriptor and value, per family */
    if (frame->di == SANXING_DI_READING) {          /* E002 reading */
        if (payload_len < SANXING_E002_OFF_VALUE) {
            return SANXING_ERR_SHORT_PAYLOAD;
        }
        out->code[0] = (char)payload[SANXING_E002_OFF_CODE + 0];
        out->code[1] = (char)payload[SANXING_E002_OFF_CODE + 1];
        out->code[2] = (char)payload[SANXING_E002_OFF_CODE + 2];
        out->code[3] = '\0';
        out->meter_status = payload[SANXING_E002_OFF_STATUS];
        out->descriptor   = (uint16_t)((payload[SANXING_E002_OFF_DESCR + 1] << 8) |
                                        payload[SANXING_E002_OFF_DESCR + 0]);
        value_start = SANXING_E002_OFF_VALUE;
    } else if (frame->di == SANXING_DI_TOKEN) {  /* E001 token */
        if (payload_len < SANXING_E001_OFF_VALUE) {
            return SANXING_ERR_SHORT_PAYLOAD;
        }
        out->code[0] = '\0';
        out->meter_status = payload[SANXING_E001_OFF_STATUS];
        out->descriptor   = (uint16_t)((payload[SANXING_E001_OFF_DESCR + 1] << 8) |
                                        payload[SANXING_E001_OFF_DESCR + 0]);
        value_start = SANXING_E001_OFF_VALUE;
    } else {
        return SANXING_ERR_UNKNOWN_DI;
    }

    /* find the value marker */
    int marker_at = -1;
    if (payload_len >= SANXING_VALUE_MARKER_LEN) {
        for (uint8_t pos = value_start; pos + SANXING_VALUE_MARKER_LEN <= payload_len; pos++) {
            if (memcmp(&payload[pos], SANXING_VALUE_MARKER, SANXING_VALUE_MARKER_LEN) == 0) {
                marker_at = pos;
                break;
            }
        }
    }

    uint8_t value_end = (marker_at >= 0) ? (uint8_t)marker_at : payload_len;
    out->raw     = &payload[value_start];
    out->raw_len = (value_end > value_start) ? (uint8_t)(value_end - value_start) : 0;

    /* trailer only exists on long replies, i.e. when the marker is present */
    if (marker_at >= 0 && payload_len >= SANXING_TRAILER_LEN) {
        const uint8_t *trailer = &payload[payload_len - SANXING_TRAILER_LEN];
        out->balance      = read_uint_le(trailer, 4);
        out->total_energy = read_uint_le(trailer + 4, 4);
        out->has_trailer  = true;
    }

    /* table lookup, then decode the value */
    const char *code = (frame->di == SANXING_DI_READING) ? out->code : NULL;
    out->desc = sanxing_lookup_descriptor(out->descriptor, code);
    out->divisor = out->desc ? out->desc->divisor : 1;

    Sanxing_Encoding encoding = out->desc ? out->desc->encoding : SANXING_ENC_BYTES;

    switch (encoding) {
    case SANXING_ENC_U16LE:
        out->value = (int32_t)read_uint_le(out->raw, out->raw_len < 2 ? out->raw_len : 2);
        out->has_value = true;
        break;
    case SANXING_ENC_U32LE:
        out->value = (int32_t)read_uint_le(out->raw, out->raw_len < 4 ? out->raw_len : 4);
        out->has_value = true;
        break;
    case SANXING_ENC_BCD_DATE:
        if (out->raw_len >= 4) {
            out->datetime.weekday = bcd(out->raw[0]);
            out->datetime.year    = bcd(out->raw[1]);
            out->datetime.month   = bcd(out->raw[2]);
            out->datetime.day     = bcd(out->raw[3]);
            out->has_datetime = true;
        }
        break;
    case SANXING_ENC_BCD_TIME:
        if (out->raw_len >= 3) {
            out->datetime.hour   = bcd(out->raw[0]);
            out->datetime.minute = bcd(out->raw[1]);
            out->datetime.second = bcd(out->raw[2]);
            out->has_datetime = true;
        }
        break;
    case SANXING_ENC_ASCII_NUM: {
        char text_buf[GW_READING_TEXT_MAX];
        tail_ascii(out->raw, out->raw_len, text_buf, sizeof(text_buf));
        out->value = ascii_to_int(text_buf);
        out->has_value = true;
        break;
    }
    case SANXING_ENC_TEXT:
        tail_ascii(out->raw, out->raw_len, out->text, sizeof(out->text));
        out->has_text = (out->text[0] != '\0');
        break;
    case SANXING_ENC_BYTES:
    default:
        /* located but not interpreted; raw/raw_len are left for the caller */
        break;
    }

    return SANXING_OK;
}
