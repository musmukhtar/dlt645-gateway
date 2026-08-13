#ifndef GW_READING_H
#define GW_READING_H

#include <stdint.h>
#include <stdbool.h>

/* Protocol-neutral reading model.  Every driver maps its decoded values
   into this struct; nothing above the drivers names a brand.

   One frame produces 1..N readings.  A Sanxing long reply yields the main
   reading plus the trailer's balance and total energy. */

/* Which brand produced the reading. */
typedef enum {
    GW_PROTO_UNKNOWN = 0,
    GW_PROTO_SANXING,        /* DL/T 645, Sanxing CIU-MH03 */
    GW_PROTO_CONLOG          /* reserved, not yet implemented */
} gw_protocol_t;

/* Each driver maps its native data items onto these, so "voltage" means
   the same thing whatever the brand. */
typedef enum {
    GW_Q_UNKNOWN = 0,
    GW_Q_VOLTAGE,
    GW_Q_CURRENT,
    GW_Q_POWER,
    GW_Q_POWER_FACTOR,
    GW_Q_FREQUENCY,
    GW_Q_ENERGY_TOTAL,       /* lifetime import */
    GW_Q_ENERGY_PERIOD,      /* a month/day bucket */
    GW_Q_MAX_DEMAND,
    GW_Q_CREDIT,             /* remaining prepaid credit */
    GW_Q_BALANCE,            /* trailer balance value */
    GW_Q_SUPPLY_DAYS,
    GW_Q_TARIFF,
    GW_Q_METER_ID,
    GW_Q_DATE,
    GW_Q_TIME,
    GW_Q_TOKEN_RESULT,       /* recharge outcome (accepted/used/reject) */
    GW_Q_THRESHOLD,          /* over/under-voltage disconnect limit */
    GW_Q_RAW                 /* decoded location known, meaning still raw */
} gw_quantity_t;

/* What the meter said about this reading. */
typedef enum {
    GW_ST_OK = 0,
    GW_ST_METER_ERROR,       /* meter flagged an error on the reading */
    GW_ST_REFUSED            /* command refused (e.g. needs authorization) */
} gw_reading_status_t;

#define GW_READING_TEXT_MAX  16   /* longest reply text seen is "rEJECt" */
#define GW_METER_ID_MAX       8

/* One decoded value. */
typedef struct {
    gw_protocol_t       protocol;
    uint8_t             meter_id[GW_METER_ID_MAX];  /* raw, brand-specific */
    uint8_t             meter_id_len;
    gw_quantity_t       quantity;
    const char         *label;      /* "Voltage L-A", static string */
    const char         *unit;       /* "V", "kWh", or "" */
    gw_reading_status_t status;

    /* numeric value, already scaled and ready to use */
    bool                has_value;
    double              value;

    /* text replies ("UsEd", "rEJECt") instead of a number */
    bool                has_text;
    char                text[GW_READING_TEXT_MAX];

    /* raw value bytes, for containers and debugging.  Points into the
       driver's scratch buffer and is valid only until the next decode. */
    const uint8_t      *raw;
    uint8_t             raw_len;
} gw_reading_t;

#endif /* GW_READING_H */
