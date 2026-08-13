#ifndef GW_REGISTRY_H
#define GW_REGISTRY_H

#include "gw/gw_protocol.h"

/* The driver objects, one per meter brand.  Each is defined in its own
   translation unit and listed in the registry in src/core/gw_session.c.
   Declared extern so the object has external linkage in C++ too, which
   gives file-scope const internal linkage otherwise. */

extern const gw_driver_t sanxing_driver;
extern const gw_driver_t conlog_driver;

#endif /* GW_REGISTRY_H */
