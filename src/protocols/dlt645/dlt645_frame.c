/* DL/T 645-1997 wire framing.  Pure function over a byte buffer.

   Two rules that are easy to get wrong:
     - Do not search for 0x68 or 0x16.  Both occur inside masked data: a
       data byte of 0x35 (ASCII '5') transmits as 0x68.  Compute positions
       from L instead, then check the byte is where it should be.
     - Do not use L before bounds-checking 10 + L + 2 against the buffer.
   The checksum is what actually proves a frame is genuine. */

#include "dlt645_frame.h"

uint8_t dlt645_checksum(const uint8_t *buf, size_t count)
{
    uint32_t sum = 0;
    for (size_t pos = 0; pos < count; pos++) {
        sum += buf[pos];
    }
    return (uint8_t)(sum & 0xFF);
}

DLT645_Result dlt645_parse_frame(const uint8_t *buf, size_t len,
                                 DLT645_Frame_Info *out)
{
    if (buf == NULL || out == NULL) {
        return DLT645_ERR_NULL;
    }

    /* minimum size */
    if (len < DLT645_FRAME_MIN) {
        return DLT645_ERR_TOO_SHORT;
    }

    /* both start bytes; buf[7] is the stronger filter */
    if (buf[DLT645_OFF_START1] != DLT645_START_BYTE ||
        buf[DLT645_OFF_START2] != DLT645_START_BYTE) {
        return DLT645_ERR_NO_START;
    }

    /* data length, then bound the whole frame before trusting it */
    uint8_t data_len = buf[DLT645_OFF_LENGTH];
    size_t total = (size_t)DLT645_OFF_DATA + data_len + 2;   /* data + CS + stop */
    if (total > len) {
        return DLT645_ERR_BAD_LENGTH;
    }
    if (data_len < DLT645_DI_LEN) {
        return DLT645_ERR_BAD_LENGTH;     /* too short to hold a DI */
    }
    if (data_len > DLT645_DATA_CAP) {
        return DLT645_ERR_BAD_LENGTH;     /* larger than the buffer: reject */
    }

    /* stop byte, at the computed position */
    if (buf[total - 1] != DLT645_STOP_BYTE) {
        return DLT645_ERR_NO_STOP;
    }

    /* checksum over 0x68 .. last data byte */
    uint8_t checksum_pos = DLT645_OFF_DATA + data_len;
    if (dlt645_checksum(buf, checksum_pos) != buf[checksum_pos]) {
        return DLT645_ERR_CHECKSUM;
    }

    /* address: raw BCD, not masked */
    for (int addr_pos = 0; addr_pos < DLT645_ADDR_LEN; addr_pos++) {
        out->address[addr_pos] = buf[DLT645_OFF_ADDR + addr_pos];
    }

    /* only D7 matters: request vs reply */
    out->is_response = (buf[DLT645_OFF_CONTROL] & DLT645_CONTROL_RESPONSE_BYTE) != 0;

    /* unmask into out->data */
    for (uint8_t data_pos = 0; data_pos < data_len; data_pos++) {
        out->data[data_pos] = (uint8_t)(buf[DLT645_OFF_DATA + data_pos] - DLT645_DATA_MASK);
    }

    /* DI, low byte first */
    out->di = (uint16_t)((out->data[DLT645_DI_OFF_HIGH] << 8) |
                          out->data[DLT645_DI_OFF_LOW]);

    /* payload starts after the DI */
    out->payload     = out->data + DLT645_DI_LEN;
    out->payload_len = (uint8_t)(data_len - DLT645_DI_LEN);

    return DLT645_OK;
}
