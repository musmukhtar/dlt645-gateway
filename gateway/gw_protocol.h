#ifndef GW_PROTOCOL_H
#define GW_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "gw_reading.h"

/*======================================================================
 *  gw_protocol.h  --  the pluggable meter-driver interface + binding.
 *
 *  One gateway is wired to ONE meter, so we pick a driver ONCE at startup
 *  and decode every later frame through it.  There is no per-frame
 *  protocol sniffing.
 *
 *      startup:   gw_bind(&s, GW_PROTO_SANXING)         (from config)
 *             or  gw_bind_auto(&s, first_frame, len)    (auto-detect)
 *      per frame: gw_decode(&s, frame, len, out, max, &count)
 *
 *  Adding Conlog later = write one driver and register it.  Nothing in
 *  the gateway core changes.
 *====================================================================*/

typedef enum {
    GW_OK = 0,
    GW_ERR_NOT_MINE,       /* probe: this frame is not my protocol      */
    GW_ERR_BAD_FRAME,      /* framing/checksum failed                   */
    GW_ERR_OVERFLOW,       /* more readings than the caller's array     */
    GW_ERR_UNSUPPORTED,    /* driver exists but cannot decode this yet  */
    GW_ERR_NO_DRIVER,      /* no driver bound / none matched            */
    GW_ERR_NULL            /* NULL argument                             */
} gw_result_t;

/* One meter brand.  Filled in by each driver's translation unit. */
typedef struct {
    gw_protocol_t protocol;
    const char   *name;

    /* Probe: does this sample look like a complete frame of my protocol?
       Used ONCE at bind time for auto-detect.  Treats buf as read-only. */
    bool (*identify)(const uint8_t *buf, size_t len);

    /* Decode one frame into 1..N common readings.  buf is read-only; a
       driver that must mutate (Sanxing -0x33 unmask) copies into its own
       scratch first.  On success writes *count and returns GW_OK. */
    gw_result_t (*decode)(const uint8_t *buf, size_t len,
                          gw_reading_t *out, size_t max, size_t *count);

    /* FUTURE SEAM, may be NULL: build a command frame such as a recharge
       token.  This is where the write path (PROTOCOL_REFERENCE.md section
       17) will live.  Not implemented for any driver yet. */
    gw_result_t (*encode)(int cmd, const void *args,
                          uint8_t *out, size_t max, size_t *len);
} gw_driver_t;

/* The one driver chosen for this gateway. */
typedef struct {
    const gw_driver_t *driver;
} gw_session_t;

/* --- registry access (implemented in gw_session.c) --- */

/* Number of registered drivers, and indexed access, for tests/tools. */
size_t             gw_driver_count(void);
const gw_driver_t *gw_driver_at(size_t index);

/* --- binding + decode --- */

/* Force-select by protocol id (from provisioning/config).
   Returns GW_ERR_NO_DRIVER if no driver serves that protocol. */
gw_result_t gw_bind(gw_session_t *session, gw_protocol_t proto);

/* Auto-detect once: run each registered driver's identify() on a sample
   frame and bind the first match.  Returns GW_ERR_NO_DRIVER if none match. */
gw_result_t gw_bind_auto(gw_session_t *session, const uint8_t *sample, size_t len);

/* Per-frame decode through the already-bound driver.  No demux.
   Returns GW_ERR_NO_DRIVER if the session is not bound. */
gw_result_t gw_decode(gw_session_t *session, const uint8_t *buf, size_t len,
                      gw_reading_t *out, size_t max, size_t *count);

const char *gw_result_to_string(gw_result_t result);

#endif /* GW_PROTOCOL_H */
