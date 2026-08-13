/* Conlog brand slot, not implemented.  Registered so the gateway can
   already select Conlog, but inert: the probe matches nothing and decode
   reports unsupported.  Fill these in when captures exist. */

#include "gw/gw_protocol.h"
#include "gw/gw_registry.h"   /* external-linkage decl for conlog_driver */

static bool conlog_identify(const uint8_t *buf, size_t len)
{
    (void)buf;
    (void)len;
    return false;   /* no Conlog frame format known yet */
}

static gw_result_t conlog_decode(const uint8_t *buf, size_t len,
                                 gw_reading_t *out, size_t max, size_t *count)
{
    (void)buf;
    (void)len;
    (void)out;
    (void)max;
    if (count != NULL) {
        *count = 0;
    }
    return GW_ERR_UNSUPPORTED;
}

const gw_driver_t conlog_driver = {
    .protocol = GW_PROTO_CONLOG,
    .name     = "Conlog (stub)",
    .identify = conlog_identify,
    .decode   = conlog_decode,
    .encode   = NULL,
};
