#ifndef DLT645_FRAME_H
#define DLT645_FRAME_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* DL/T 645-1997 wire framing: 68 [addr] 68 C L [data] CS 16, the +0x33 data
   mask, and the checksum.  Field positions only, no vendor meaning.
   Keep this file vendor-neutral; data-identifier meanings go in a profile. */

/* --- frame bytes --- */
#define DLT645_START_BYTE            0x68
#define DLT645_STOP_BYTE             0x16

/* Control byte, DL/T 645-1997 clause 5.2.3:
     D7     0 = master command, 1 = slave response
     D6     slave abnormal
     D5     more frames follow
     D4..D0 function code
   This meter only ever sends 0x00 or 0x80, function code 00000, so only D7
   is decoded. */
#define DLT645_CONTROL_REQUEST_BYTE  0x00   /* master -> meter */
#define DLT645_CONTROL_RESPONSE_BYTE 0x80   /* meter  -> master */

/* --- the +0x33 data mask --- */
#define DLT645_DATA_MASK             0x33

/* --- sizes --- */
#define DLT645_ADDR_LEN              6
#define DLT645_DATA_MAX              255   /* protocol limit: L is one byte */
#define DLT645_FRAME_MIN             12    /* 68 +6addr +68 +C +L +CS +16, L=0 */

/* Unmask buffer size.  L can be up to 255, but the largest frame this meter
   sends is L=56.  Frames with L above the cap are rejected, not truncated.
   Raise it if your meter sends bigger frames. */
#define DLT645_DATA_CAP              64

/* --- fixed positions inside the frame (never search for them) --- */
#define DLT645_OFF_START1            0
#define DLT645_OFF_ADDR              1
#define DLT645_OFF_START2            7
#define DLT645_OFF_CONTROL           8
#define DLT645_OFF_LENGTH            9
#define DLT645_OFF_DATA              10

/* Data identifier: first two data bytes, low byte first, so
   di = (data[HIGH] << 8) | data[LOW].  Payload follows.  Which DI values a
   meter uses is vendor-specific. */
#define DLT645_DI_OFF_LOW   0
#define DLT645_DI_OFF_HIGH  1
#define DLT645_DI_LEN       2

/* Framing errors only.  A well-framed but unreadable payload is the
   profile layer's verdict (Sanxing_Result). */
typedef enum {
    DLT645_OK = 0,
    DLT645_ERR_NULL,             /* NULL argument                      */
    DLT645_ERR_TOO_SHORT,        /* fewer than DLT645_FRAME_MIN bytes  */
    DLT645_ERR_NO_START,         /* buf[0] or buf[7] is not 0x68       */
    DLT645_ERR_BAD_LENGTH,       /* 10 + L + 2 does not fit the buffer */
    DLT645_ERR_NO_STOP,          /* stop byte is not 0x16              */
    DLT645_ERR_CHECKSUM          /* the real proof failed              */
} DLT645_Result;

/* Owns its storage: the unmasked data field goes into `data` and `payload`
   points into it.  Valid until the next parse overwrites the struct. */
typedef struct {
    bool           is_response;           /* control D7: false=request, true=reply */
    uint8_t        address[DLT645_ADDR_LEN]; /* raw BCD, LE on the wire  */
    uint16_t       di;                    /* vendor-defined, see profile */
    const uint8_t *payload;               /* -> data + 2 (after the DI)  */
    uint8_t        payload_len;           /* = L - 2                     */
    uint8_t        data[DLT645_DATA_CAP]; /* the unmasked data field     */
} DLT645_Frame_Info;

/* Parse, validate and unmask one frame.  buf is not modified.
   Checks in order: length, buf[0], buf[7], computed stop byte, checksum.
   Every access is bounded against len. */
DLT645_Result dlt645_parse_frame(const uint8_t *buf, size_t len,
                                 DLT645_Frame_Info *out);

/* Checksum over buf[0..count-1], low 8 bits. */
uint8_t dlt645_checksum(const uint8_t *buf, size_t count);

#endif /* DLT645_FRAME_H */
