/* Replay the 39 captures through the gateway and check the decoded values
   against the ones confirmed from the meter's own display. */

#include <stdio.h>
#include <string.h>
#include <math.h>
#include "gw/gw_protocol.h"
#include "captures.h"

static int g_pass = 0, g_fail = 0;

static void check(int passed, const char *description)
{
    if (passed) { g_pass++; }
    else { g_fail++; printf("  FAIL: %s\n", description); }
}

static int approx(double actual, double expected)
{
    return fabs(actual - expected) < 0.01;
}

/* find the first capture whose name contains name_substr */
static int find(const char *name_substr)
{
    for (int index = 0; index < CAPTURE_COUNT; index++) {
        if (strstr(CAPTURES[index].name, name_substr)) return index;
    }
    return -1;
}

/* decode one capture through a Sanxing-bound session; returns reading count */
static size_t decode_capture(gw_session_t *session, int capture_index,
                             gw_reading_t *out, size_t max)
{
    size_t count = 0;
    gw_result_t result = gw_decode(session, CAPTURES[capture_index].res,
                                   CAPTURES[capture_index].res_len, out, max, &count);
    if (result != GW_OK) {
        printf("  decode(%s) -> %s\n", CAPTURES[capture_index].name,
               gw_result_to_string(result));
        return 0;
    }
    return count;
}

/* check the main (reading 0) numeric value of a named capture */
static void check_value(gw_session_t *session, const char *name_substr,
                        double expected, const char *unit)
{
    int capture_index = find(name_substr);
    gw_reading_t readings[4];
    if (capture_index < 0) { check(0, name_substr); return; }
    size_t count = decode_capture(session, capture_index, readings, 4);
    char message[128];
    snprintf(message, sizeof(message), "%s = %.3f %s", name_substr, expected, unit);
    check(count >= 1 && readings[0].has_value && approx(readings[0].value, expected),
          message);
}

int main(void)
{
    printf("== bind paths ==\n");
    gw_session_t session;

    check(gw_bind(&session, GW_PROTO_SANXING) == GW_OK && session.driver &&
          session.driver->protocol == GW_PROTO_SANXING,
          "gw_bind(SANXING) selects sanxing");

    /* auto-probe with the first capture should also pick Sanxing */
    gw_session_t probe_session;
    check(gw_bind_auto(&probe_session, CAPTURES[0].res, CAPTURES[0].res_len) == GW_OK &&
          probe_session.driver &&
          probe_session.driver->protocol == GW_PROTO_SANXING,
          "gw_bind_auto() picks sanxing from a real frame");

    /* Conlog must never claim a Sanxing frame */
    const gw_driver_t *conlog = NULL;
    for (size_t index = 0; index < gw_driver_count(); index++) {
        const gw_driver_t *driver = gw_driver_at(index);
        if (driver && driver->protocol == GW_PROTO_CONLOG) conlog = driver;
    }
    check(conlog != NULL, "conlog driver is registered");
    if (conlog) {
        int any_matched = 0;
        for (int capture_index = 0; capture_index < CAPTURE_COUNT; capture_index++) {
            if (conlog->identify(CAPTURES[capture_index].res,
                                 CAPTURES[capture_index].res_len)) any_matched = 1;
        }
        check(!any_matched, "conlog identify() rejects all 39 sanxing frames");
    }

    printf("== ground-truth values ==\n");
    check_value(&session, "Voltage, line A",            239.0,   "V");
    check_value(&session, "Grid frequency",             49.97,   "Hz");
    check_value(&session, "Power factor",               1.000,   "");
    check_value(&session, "Remaining supply time",      9999,    "days");
    check_value(&session, "Reminder credit",            162.11,  "kWh");
    check_value(&session, "Total active energy import", 8937.39, "kWh");
    check_value(&session, "Energy three months ago",    74.43,   "kWh");
    check_value(&session, "Current date",               20260804,"(YYYYMMDD)");
    check_value(&session, "Current time",               142431,  "(HHMMSS)");
    check_value(&session, "Over-voltage disconnect",    270.8,   "V");
    check_value(&session, "Under-voltage disconnect",   120.8,   "V");

    printf("== tokens ==\n");
    check_value(&session, "Accepted token",             2.90,    "kWh");   /* index 0 */
    check_value(&session, "token 2",                    42.90,   "kWh");

    /* short token replies: text set, no trailer */
    {
        gw_reading_t readings[4];
        size_t count = decode_capture(&session, find("Used token"), readings, 4);
        check(count == 1 && readings[0].has_text &&
              strcmp(readings[0].text, "UsEd") == 0, "used token -> text \"UsEd\"");
        check(count == 1, "used token has no trailer readings");
    }
    {
        gw_reading_t readings[4];
        size_t count = decode_capture(&session, find("Rejected token (invalid)"),
                                      readings, 4);
        check(count == 1 && readings[0].has_text &&
              strcmp(readings[0].text, "rEJECt") == 0,
              "rejected token -> text \"rEJECt\"");
    }

    printf("== trailer + meter id ==\n");
    {
        gw_reading_t readings[4];
        size_t count = decode_capture(&session, find("Voltage, line A"), readings, 4);
        /* main + balance + total energy */
        check(count == 3, "long reply emits 3 readings (main + trailer x2)");
        int balance_idx = -1, energy_idx = -1;
        for (size_t index = 0; index < count; index++) {
            if (readings[index].quantity == GW_Q_BALANCE)      balance_idx = (int)index;
            if (readings[index].quantity == GW_Q_ENERGY_TOTAL) energy_idx  = (int)index;
        }
        check(balance_idx >= 0 && approx(readings[balance_idx].value, 162.11),
              "trailer balance = 162.11 kWh");
        check(energy_idx >= 0 && approx(readings[energy_idx].value, 8937.39),
              "trailer total energy = 8937.39 kWh");

        static const uint8_t expected_id[6] = {0x14,0x94,0x92,0x70,0x50,0x04};
        check(readings[0].meter_id_len == 6 &&
              memcmp(readings[0].meter_id, expected_id, 6) == 0,
              "meter id = 14 94 92 70 50 04");
    }

    printf("== decode all 39 without error ==\n");
    {
        int failures = 0;
        for (int capture_index = 0; capture_index < CAPTURE_COUNT; capture_index++) {
            gw_reading_t readings[4];
            size_t count = 0;
            if (gw_decode(&session, CAPTURES[capture_index].res,
                          CAPTURES[capture_index].res_len, readings, 4, &count) != GW_OK)
                failures++;
        }
        check(failures == 0, "all 39 captures decode without error");
    }

    printf("== request frames are rejected, not mis-decoded ==\n");
    {
        /* a real code-111 REQUEST (control 0x00): DI + "111", no reading */
        static const uint8_t request[] = {
            0x68,0x14,0x94,0x92,0x70,0x50,0x04,0x68,0x00,0x05,
            0x35,0x13,0x64,0x64,0x64,0x47,0x16
        };
        gw_reading_t readings[4];
        size_t count = 1;
        gw_result_t result = gw_decode(&session, request, sizeof(request),
                                       readings, 4, &count);
        check(result == GW_ERR_BAD_FRAME && count == 0,
              "request frame -> rejected (not decoded as a reading)");
    }

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
