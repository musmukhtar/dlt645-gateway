#ifndef DLT645_H
#define DLT645_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/*======================================================================
 *  dlt645.h  --  DL/T 645-1997 FRAME LAYER (standard, frozen).
 *
 *  This half is the plain DL/T 645 envelope: 68 [addr] 68 C L [data] CS 16,
 *  the +0x33 data mask, and the checksum.  It knows nothing about what the
 *  data means.  The vendor meaning (descriptors, values, balance) lives in
 *  dlt645_sanxing.h / .c so a new capture never forces an edit here.
 *
 *  Meter: Sanxing CIU-MH03 (Sudan).  Full protocol notes in
 *  PROTOCOL_REFERENCE.md.
 *====================================================================*/

/* --- frame bytes --- */
#define DLT645_START_BYTE            0x68
#define DLT645_STOP_BYTE             0x16

/* --- control byte (D7..D0), per DL/T 645-1997 clause 5.2.3 ------------
 *   D7     transfer direction : 0 = master command, 1 = slave response
 *   D6     slave abnormal      : 0 = correct,        1 = abnormal
 *   D5     subsequent frame    : 0 = none,           1 = more data follows
 *   D4..D0 function code       : 00001 read, 00100 write, ... (see spec)
 *
 * This meter uses ONLY the two whole-byte values below, and its function
 * code is 00000 (the spec's "reserved").  It does not use the standard
 * read/write codes: the user-defined DI (E002) plus the 3-digit code do
 * the selecting.  So we keep just these two constants, decode no bits, and
 * a future request builder simply writes REQUEST_BYTE.                    */
#define DLT645_CONTROL_REQUEST_BYTE  0x00   /* master -> meter */
#define DLT645_CONTROL_RESPONSE_BYTE 0x80   /* meter  -> master */

/* --- the +0x33 data mask --- */
#define DLT645_DATA_MASK             0x33

/* --- sizes --- */
#define DLT645_ADDR_LEN              6
#define DLT645_DATA_MAX              255   /* L is one byte */
#define DLT645_FRAME_MIN             12    /* 68 +6addr +68 +C +L +CS +16, L=0 */
#define DLT645_FRAME_MAX             (10 + DLT645_DATA_MAX + 2)

/* --- fixed positions inside the frame (never search for them) --- */
#define DLT645_OFF_START1            0
#define DLT645_OFF_ADDR              1
#define DLT645_OFF_START2            7
#define DLT645_OFF_CONTROL           8
#define DLT645_OFF_LENGTH            9
#define DLT645_OFF_DATA              10

/* --- outer data identifier (transmitted LE: DI0 then DI1) --- */
#define DLT645_TOKEN_DATA_IDENTIFIER 0xE001  /* token / recharge ops */
#define DLT645_HASH_DATA_IDENTIFIER  0xE002  /* meter readings        */

/*----------------------------------------------------------------------
 *  Result of any decode step (frame layer + vendor layer share it).
 *--------------------------------------------------------------------*/
typedef enum {
    DLT645_OK = 0,
    /* frame layer */
    DLT645_ERR_TOO_SHORT,        /* fewer than DLT645_FRAME_MIN bytes  */
    DLT645_ERR_NO_START,         /* buf[0] or buf[7] is not 0x68       */
    DLT645_ERR_BAD_LENGTH,       /* 10 + L + 2 does not fit the buffer */
    DLT645_ERR_NO_STOP,          /* stop byte is not 0x16              */
    DLT645_ERR_CHECKSUM,         /* the real proof failed              */
    DLT645_ERR_NOT_RESPONSE,     /* a request frame, not a reply        */
    DLT645_ERR_NULL,             /* NULL argument                      */
    /* vendor layer (see dlt645_sanxing.h) */
    DLT645_ERR_UNKNOWN_DI,       /* not E001 and not E002              */
    DLT645_ERR_SHORT_PAYLOAD,    /* too short for code+status+descr    */
    DLT645_ERR_UNKNOWN_REGISTER  /* descriptor not in the table        */
} DLT645_Result;

/*----------------------------------------------------------------------
 *  Output of the frame layer.  It owns its own storage: the unmasked data
 *  field is written into `data`, and `payload` points into it.  So the
 *  struct is self-contained -- valid until the next parse reuses it, with
 *  no separate scratch buffer for the caller to manage.
 *--------------------------------------------------------------------*/
typedef struct {
    bool           is_response;           /* control D7: false=request, true=reply */
    uint8_t        address[DLT645_ADDR_LEN]; /* raw BCD, LE on the wire  */
    uint16_t       di;                    /* 0xE001 or 0xE002            */
    const uint8_t *payload;               /* -> data + 2 (after the DI)  */
    uint8_t        payload_len;           /* = L - 2                     */
    uint8_t        data[DLT645_DATA_MAX]; /* the unmasked data field     */
} DLT645_Frame_Info;

/*----------------------------------------------------------------------
 *  Parse + validate + unmask one frame.
 *
 *  buf/len : the raw frame as received (READ-ONLY, not modified).
 *  out     : filled in; owns the unmasked data (see DLT645_Frame_Info).
 *
 *  Steps, in order: length, buf[0]==0x68, buf[7]==0x68, computed stop
 *  byte, checksum, then unmask and split the DI.  Every access is bounded
 *  against len.
 *--------------------------------------------------------------------*/
DLT645_Result dlt645_parse_frame(const uint8_t *buf, size_t len,
                                 DLT645_Frame_Info *out);

/* Convenience: DL/T 645 checksum over buf[0..n-1], low 8 bits. */
uint8_t dlt645_checksum(const uint8_t *buf, size_t n);

#endif /* DLT645_H */
