/*======================================================================
 *  sanxing_driver.c  --  the Sanxing brand as a gw_driver_t.
 *
 *  Wraps the pure DL/T 645 + Sanxing decoders and maps their native
 *  DLT645_Reading into the brand-neutral gw_reading_t[].  A long reply
 *  emits the main reading plus the trailer's balance and total energy.
 *====================================================================*/

#include "gw_protocol.h"
#include "gw_drivers.h"   /* external-linkage decl for sanxing_driver */
#include "dlt645.h"
#include "dlt645_sanxing.h"
#include <string.h>

/* Scratch for the -0x33 unmask.  The interface buffer is read-only, so the
   mutation happens here, never in the caller's bytes.  One meter, one
   in-flight frame, so a single static buffer is fine (not reentrant). */
static uint8_t g_scratch[DLT645_FRAME_MAX];

/*----------------------------------------------------------------------
 *  identify: the one-time bind-probe.  Accept only a complete, checksum-
 *  valid DL/T 645 frame.  Read-only on buf.
 *--------------------------------------------------------------------*/
static bool sanxing_identify(const uint8_t *buf, size_t len)
{
    if (buf == NULL || len < DLT645_FRAME_MIN) {
        return false;
    }
    if (buf[DLT645_OFF_START1] != DLT645_START_BYTE ||
        buf[DLT645_OFF_START2] != DLT645_START_BYTE) {
        return false;
    }
    uint8_t L = buf[DLT645_OFF_LENGTH];
    size_t total = (size_t)DLT645_OFF_DATA + L + 2;
    if (total > len || L < 2) {
        return false;
    }
    if (buf[total - 1] != DLT645_STOP_BYTE) {
        return false;
    }
    uint8_t cs_pos = (uint8_t)(DLT645_OFF_DATA + L);
    return dlt645_checksum(buf, cs_pos) == buf[cs_pos];
}

/* Fill the common fields shared by every reading from one frame. */
static void base_reading(gw_reading_t *r, const DLT645_Frame_Info *fi)
{
    memset(r, 0, sizeof(*r));
    r->protocol = GW_PROTO_SANXING;
    memcpy(r->meter_id, fi->address, DLT645_ADDR_LEN);
    r->meter_id_len = DLT645_ADDR_LEN;
    r->status = GW_ST_OK;
}

static gw_reading_status_t map_status(uint8_t meter_status)
{
    switch (meter_status) {
    case DLT645_METER_OK:     return GW_ST_OK;
    case DLT645_METER_ERROR:  return GW_ST_METER_ERROR;
    case DLT645_METER_FAILED: return GW_ST_REFUSED;
    default:                  return GW_ST_METER_ERROR;
    }
}

/*----------------------------------------------------------------------
 *  decode: parse + vendor-decode + map to gw_reading_t[].
 *--------------------------------------------------------------------*/
static gw_result_t sanxing_decode(const uint8_t *buf, size_t len,
                                  gw_reading_t *out, size_t max, size_t *count)
{
    *count = 0;

    DLT645_Frame_Info fi;
    DLT645_Result fr = dlt645_parse_frame(buf, len, g_scratch,
                                          sizeof(g_scratch), &fi);
    if (fr != DLT645_OK) {
        return GW_ERR_BAD_FRAME;
    }

    DLT645_Reading rd;
    DLT645_Result dr = dlt645_decode_reading(&fi, &rd);
    if (dr != DLT645_OK) {
        return GW_ERR_BAD_FRAME;
    }

    if (max < 1) {
        return GW_ERR_OVERFLOW;
    }

    /* --- reading 0: the main value --- */
    gw_reading_t *r = &out[0];
    base_reading(r, &fi);
    r->status   = map_status(rd.meter_status);
    r->quantity = rd.reg ? rd.reg->quantity : GW_Q_UNKNOWN;
    r->label    = rd.reg ? rd.reg->label : "Unknown register";
    r->unit     = rd.reg ? rd.reg->unit  : "";
    r->raw      = rd.raw;
    r->raw_len  = rd.raw_len;

    if (rd.has_value) {
        r->has_value = true;
        r->value = (rd.divisor > 1)
                       ? (double)rd.value / (double)rd.divisor
                       : (double)rd.value;
    }
    if (rd.has_text) {
        r->has_text = true;
        memcpy(r->text, rd.text, sizeof(r->text));
    }
    if (rd.has_datetime) {
        /* expose date/time as a packed numeric so the model stays flat:
           date -> YYYYMMDD, time -> HHMMSS */
        r->has_value = true;
        if (rd.reg && rd.reg->quantity == GW_Q_DATE) {
            r->value = (2000 + rd.datetime.year) * 10000.0
                     + rd.datetime.month * 100.0 + rd.datetime.day;
        } else {
            r->value = rd.datetime.hour * 10000.0
                     + rd.datetime.minute * 100.0 + rd.datetime.second;
        }
    }
    *count = 1;

    /* --- readings 1..2: the standing trailer registers --- */
    if (rd.has_trailer) {
        if (*count < max) {
            gw_reading_t *b = &out[*count];
            base_reading(b, &fi);
            b->quantity  = GW_Q_BALANCE;
            b->label     = "Balance";
            b->unit      = "kWh";
            b->has_value = true;
            b->value     = (double)rd.balance / 100.0;
            (*count)++;
        }
        if (*count < max) {
            gw_reading_t *e = &out[*count];
            base_reading(e, &fi);
            e->quantity  = GW_Q_ENERGY_TOTAL;
            e->label     = "Total energy (trailer)";
            e->unit      = "kWh";
            e->has_value = true;
            e->value     = (double)rd.total_energy / 100.0;
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
    .encode   = NULL,   /* recharge/write path: future work */
};
