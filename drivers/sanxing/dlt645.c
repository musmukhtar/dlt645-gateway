/*======================================================================
 *  dlt645.c  --  DL/T 645-1997 FRAME LAYER.
 *
 *  Standard envelope only: 68 [addr] 68 C L [data] CS 16, the +0x33 data
 *  mask, and the checksum.  Pure function over a byte buffer, no hardware.
 *
 *  Design rules (proven in PROTOCOL_REFERENCE.md section 3):
 *   - NEVER search for 0x68 / 0x16; both occur inside masked data.  Use L
 *     to compute every position, then check the byte is there.
 *   - NEVER trust L before bounds-checking 10 + L + 2 against the buffer.
 *   - the checksum is the real proof of a genuine frame.
 *   - buf is READ-ONLY; the -0x33 unmask is written into out->data.
 *====================================================================*/

#include "dlt645.h"

uint8_t dlt645_checksum(const uint8_t *buf, size_t n)
{
    uint32_t sum = 0;
    for (size_t i = 0; i < n; i++) {
        sum += buf[i];
    }
    return (uint8_t)(sum & 0xFF);
}

DLT645_Result dlt645_parse_frame(const uint8_t *buf, size_t len,
                                 DLT645_Frame_Info *out)
{
    if (buf == NULL || out == NULL) {
        return DLT645_ERR_NULL;
    }

    /* step 1: minimum size */
    if (len < DLT645_FRAME_MIN) {
        return DLT645_ERR_TOO_SHORT;
    }

    /* steps 2 + 3: the two 0x68 start bytes (buf[7] is the strong filter) */
    if (buf[DLT645_OFF_START1] != DLT645_START_BYTE ||
        buf[DLT645_OFF_START2] != DLT645_START_BYTE) {
        return DLT645_ERR_NO_START;
    }

    /* step 4: read L, then bound the whole frame before touching data */
    uint8_t L = buf[DLT645_OFF_LENGTH];
    size_t total = (size_t)DLT645_OFF_DATA + L + 2;   /* data + CS + stop */
    if (total > len) {
        return DLT645_ERR_BAD_LENGTH;
    }
    if (L < 2) {
        /* need at least the 2-byte DI to be a decodable reading/token */
        return DLT645_ERR_BAD_LENGTH;
    }

    /* step 5: stop byte at the computed position */
    if (buf[total - 1] != DLT645_STOP_BYTE) {
        return DLT645_ERR_NO_STOP;
    }

    /* step 6: checksum over 0x68 .. last data byte */
    uint8_t cs_pos = DLT645_OFF_DATA + L;
    if (dlt645_checksum(buf, cs_pos) != buf[cs_pos]) {
        return DLT645_ERR_CHECKSUM;
    }

    /* --- frame is genuine; now read the contents --- */

    /* step 7: address (raw BCD, not masked) */
    for (int i = 0; i < DLT645_ADDR_LEN; i++) {
        out->address[i] = buf[DLT645_OFF_ADDR + i];
    }

    /* control byte (offset 8): only D7 matters -- request vs reply.
       The other bits are always 0 on this meter (see dlt645.h). */
    out->is_response = (buf[DLT645_OFF_CONTROL] & DLT645_CONTROL_RESPONSE_BYTE) != 0;

    /* step 8: unmask the data field into out->data (-0x33) */
    for (uint8_t i = 0; i < L; i++) {
        out->data[i] = (uint8_t)(buf[DLT645_OFF_DATA + i] - DLT645_DATA_MASK);
    }

    /* step 9: DI = data[1]<<8 | data[0]  (low byte first) */
    out->di = (uint16_t)((out->data[1] << 8) | out->data[0]);

    /* step 10: hand the payload (after the 2 DI bytes) to the vendor layer */
    out->payload     = out->data + 2;
    out->payload_len = (uint8_t)(L - 2);

    return DLT645_OK;
}
