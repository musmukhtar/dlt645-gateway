#ifndef GW_PROTOCOL_H
#define GW_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "gw/gw_reading.h"

/* Pluggable meter-driver interface and binding.

   A gateway is wired to one meter, so the driver is chosen once at startup
   and every later frame goes through it.  No per-frame protocol sniffing.

       startup:   gw_bind(&s, GW_PROTO_SANXING)      from config
              or  gw_bind_auto(&s, first_frame, len) auto-detect
       per frame: gw_decode(&s, frame, len, out, max, &count) */

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

    /* Does this sample look like a complete frame of my protocol?
       Called once, at bind time.  buf is read-only. */
    bool (*identify)(const uint8_t *buf, size_t len);

    /* Decode one frame into 1..N readings.  buf is read-only; a driver
       that must transform bytes copies into its own scratch first. */
    gw_result_t (*decode)(const uint8_t *buf, size_t len,
                          gw_reading_t *out, size_t max, size_t *count);

    /* Build a command frame, such as a recharge token.  May be NULL, and
       is NULL for every driver today; gw_decode checks before calling. */
    gw_result_t (*encode)(int cmd, const void *args,
                          uint8_t *out, size_t max, size_t *len);
} gw_driver_t;

/* The driver chosen for this gateway. */
typedef struct {
    const gw_driver_t *driver;
} gw_session_t;

/* Registry access. */

/* Registered driver count and indexed access, for tests and tools. */
size_t             gw_driver_count(void);
const gw_driver_t *gw_driver_at(size_t index);

/* Binding and decode. */

/* Select by protocol id, from provisioning or config. */
gw_result_t gw_bind(gw_session_t *session, gw_protocol_t proto);

/* Run each driver's identify() on one sample frame, bind the first match. */
gw_result_t gw_bind_auto(gw_session_t *session, const uint8_t *sample, size_t len);

/* Decode through the bound driver.  GW_ERR_NO_DRIVER if not bound. */
gw_result_t gw_decode(gw_session_t *session, const uint8_t *buf, size_t len,
                      gw_reading_t *out, size_t max, size_t *count);

const char *gw_result_to_string(gw_result_t result);

#endif /* GW_PROTOCOL_H */
