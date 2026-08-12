#ifndef GW_DRIVERS_H
#define GW_DRIVERS_H

#include "gw_protocol.h"

/*======================================================================
 *  gw_drivers.h  --  the concrete driver objects, one per meter brand.
 *
 *  Each is defined in its own translation unit and listed in the registry
 *  in gw_session.c.  Declared extern here so the object has external
 *  linkage in both C and C++ (C++ gives file-scope const internal linkage
 *  otherwise).  Add a line here when you add a brand.
 *====================================================================*/

extern const gw_driver_t sanxing_driver;
extern const gw_driver_t conlog_driver;

#endif /* GW_DRIVERS_H */
