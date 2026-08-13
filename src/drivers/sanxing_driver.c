/* The Sanxing brand as a gw_driver_t.  The only file that sees both the
   framing and the profile layer, and maps Sanxing_Reading into the
   brand-neutral gw_reading_t[].  A long reply emits the main reading plus
   the trailer's balance and total energy. */

#include "gw/gw_protocol.h"
#include "gw/gw_registry.h"   /* external-linkage decl for sanxing_driver */
#include "dlt645_frame.h"
#include "sanxing_profile.h"
#include <string.h>

/* Bind-time probe.  Accepts only a complete, checksum-valid frame. */
static bool sanxing_identify(const uint8_t *buf, size_t len)
{
    if (buf == NULL || len < DLT645_FRAME_MIN) {
        return false;
    }
    if (buf[DLT645_OFF_START1] != DLT645_START_BYTE ||
        buf[DLT645_OFF_START2] != DLT645_START_BYTE) {
        return false;
    }
    uint8_t data_len = buf[DLT645_OFF_LENGTH];
    size_t total = (size_t)DLT645_OFF_DATA + data_len + 2;
    if (total > len || data_len < 2) {
        return false;
    }
    if (buf[total - 1] != DLT645_STOP_BYTE) {
        return false;
    }
    uint8_t checksum_pos = (uint8_t)(DLT645_OFF_DATA + data_len);
    return dlt645_checksum(buf, checksum_pos) == buf[checksum_pos];
}

/* Fields shared by every reading from one frame. */
static void base_reading(gw_reading_t *reading, const DLT645_Frame_Info *frame)
{
    memset(reading, 0, sizeof(*reading));
    reading->protocol = GW_PROTO_SANXING;
    memcpy(reading->meter_id, frame->address, DLT645_ADDR_LEN);
    reading->meter_id_len = DLT645_ADDR_LEN;
    reading->status = GW_ST_OK;
}

static gw_reading_status_t map_status(uint8_t meter_status)
{
    switch (meter_status) {
    case SANXING_METER_OK:     return GW_ST_OK;
    case SANXING_METER_ERROR:  return GW_ST_METER_ERROR;
    case SANXING_METER_FAILED: return GW_ST_REFUSED;
    default:                  return GW_ST_METER_ERROR;
    }
}

/* Parse, decode, then map to gw_reading_t[]. */
static gw_result_t sanxing_decode(const uint8_t *buf, size_t len,
                                  gw_reading_t *out, size_t max, size_t *count)
{
    *count = 0;

    /* One meter, one in-flight frame, so a single static instance is fine.
       Not reentrant.  The readings' raw pointers stay valid until the next
       decode overwrites this. */
    static DLT645_Frame_Info frame;
    DLT645_Result parse_result = dlt645_parse_frame(buf, len, &frame);
    if (parse_result != DLT645_OK) {
        return GW_ERR_BAD_FRAME;
    }

    Sanxing_Reading reading;
    Sanxing_Result decode_result = sanxing_decode_reading(&frame, &reading);
    if (decode_result != SANXING_OK) {
        return GW_ERR_BAD_FRAME;
    }

    if (max < 1) {
        return GW_ERR_OVERFLOW;
    }

    /* reading 0: the main value */
    gw_reading_t *main_reading = &out[0];
    base_reading(main_reading, &frame);
    main_reading->status   = map_status(reading.meter_status);
    main_reading->quantity = reading.desc ? reading.desc->quantity : GW_Q_UNKNOWN;
    main_reading->label    = reading.desc ? reading.desc->label : "Unknown DI";
    main_reading->unit     = reading.desc ? reading.desc->unit  : "";
    main_reading->raw      = reading.raw;
    main_reading->raw_len  = reading.raw_len;

    if (reading.has_value) {
        main_reading->has_value = true;
        main_reading->value = (reading.divisor > 1)
                       ? (double)reading.value / (double)reading.divisor
                       : (double)reading.value;
    }
    if (reading.has_text) {
        main_reading->has_text = true;
        memcpy(main_reading->text, reading.text, sizeof(main_reading->text));
    }
    if (reading.has_datetime) {
        /* packed numeric so the model stays flat: date YYYYMMDD, time HHMMSS */
        main_reading->has_value = true;
        if (reading.desc && reading.desc->quantity == GW_Q_DATE) {
            main_reading->value = (2000 + reading.datetime.year) * 10000.0
                     + reading.datetime.month * 100.0 + reading.datetime.day;
        } else {
            main_reading->value = reading.datetime.hour * 10000.0
                     + reading.datetime.minute * 100.0 + reading.datetime.second;
        }
    }
    *count = 1;

    /* readings 1..2: the standing trailer values */
    if (reading.has_trailer) {
        if (*count < max) {
            gw_reading_t *balance_reading = &out[*count];
            base_reading(balance_reading, &frame);
            balance_reading->quantity  = GW_Q_BALANCE;
            balance_reading->label     = "Balance";
            balance_reading->unit      = "kWh";
            balance_reading->has_value = true;
            balance_reading->value     = (double)reading.balance / 100.0;
            (*count)++;
        }
        if (*count < max) {
            gw_reading_t *energy_reading = &out[*count];
            base_reading(energy_reading, &frame);
            energy_reading->quantity  = GW_Q_ENERGY_TOTAL;
            energy_reading->label     = "Total energy (trailer)";
            energy_reading->unit      = "kWh";
            energy_reading->has_value = true;
            energy_reading->value     = (double)reading.total_energy / 100.0;
            (*count)++;
        }
    }

    return GW_OK;
}

const gw_driver_t sanxing_driver = {
    .protocol = GW_PROTO_SANXING,
    .name     = "Sanxing DL/T645",
    .identify = sanxing_identify,
    .decode   = sanxing_decode,
    .encode   = NULL,   /* write path not built */
};
