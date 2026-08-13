/* Driver registry, bind-once selection, and dispatch. */

#include "gw/gw_protocol.h"
#include "gw/gw_registry.h"   /* extern decls give the objects external linkage */

/* Adding a brand means adding its object here and linking its .c file.
   Order only matters for auto-probe tie-breaks. */
static const gw_driver_t *const g_drivers[] = {
    &sanxing_driver,
    &conlog_driver,
};

#define GW_DRIVER_COUNT (sizeof(g_drivers) / sizeof(g_drivers[0]))

size_t gw_driver_count(void)
{
    return GW_DRIVER_COUNT;
}

const gw_driver_t *gw_driver_at(size_t index)
{
    if (index >= GW_DRIVER_COUNT) {
        return NULL;
    }
    return g_drivers[index];
}

gw_result_t gw_bind(gw_session_t *session, gw_protocol_t proto)
{
    if (session == NULL) {
        return GW_ERR_NULL;
    }
    session->driver = NULL;
    for (size_t index = 0; index < GW_DRIVER_COUNT; index++) {
        if (g_drivers[index]->protocol == proto) {
            session->driver = g_drivers[index];
            return GW_OK;
        }
    }
    return GW_ERR_NO_DRIVER;
}

gw_result_t gw_bind_auto(gw_session_t *session, const uint8_t *sample, size_t len)
{
    if (session == NULL || sample == NULL) {
        return GW_ERR_NULL;
    }
    session->driver = NULL;
    for (size_t index = 0; index < GW_DRIVER_COUNT; index++) {
        const gw_driver_t *driver = g_drivers[index];
        if (driver->identify != NULL && driver->identify(sample, len)) {
            session->driver = driver;
            return GW_OK;
        }
    }
    return GW_ERR_NO_DRIVER;
}

gw_result_t gw_decode(gw_session_t *session, const uint8_t *buf, size_t len,
                      gw_reading_t *out, size_t max, size_t *count)
{
    if (session == NULL || buf == NULL || out == NULL || count == NULL) {
        return GW_ERR_NULL;
    }
    *count = 0;
    if (session->driver == NULL || session->driver->decode == NULL) {
        return GW_ERR_NO_DRIVER;
    }
    return session->driver->decode(buf, len, out, max, count);
}

const char *gw_result_to_string(gw_result_t result)
{
    switch (result) {
    case GW_OK:              return "GW_OK";
    case GW_ERR_NOT_MINE:    return "GW_ERR_NOT_MINE";
    case GW_ERR_BAD_FRAME:   return "GW_ERR_BAD_FRAME";
    case GW_ERR_OVERFLOW:    return "GW_ERR_OVERFLOW";
    case GW_ERR_UNSUPPORTED: return "GW_ERR_UNSUPPORTED";
    case GW_ERR_NO_DRIVER:   return "GW_ERR_NO_DRIVER";
    case GW_ERR_NULL:        return "GW_ERR_NULL";
    default:                 return "GW_ERR_UNKNOWN";
    }
}
