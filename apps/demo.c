/* Decode every recorded capture and print the readings.  Run with `make run`. */

#include <stdio.h>
#include "gw/gw_protocol.h"
#include "captures.h"

static void print_reading(const gw_reading_t *reading)
{
    printf("    %-24s ", reading->label ? reading->label : "?");

    if (reading->has_value) {
        printf("%.3f %s", reading->value, reading->unit);
    } else if (reading->has_text) {
        printf("\"%s\"", reading->text);
    } else {
        printf("(raw, %u bytes)", reading->raw_len);
    }

    if (reading->status == GW_ST_METER_ERROR) printf("   [meter error]");
    if (reading->status == GW_ST_REFUSED)     printf("   [refused]");
    printf("\n");
}

int main(void)
{
    gw_session_t session;
    if (gw_bind(&session, GW_PROTO_SANXING) != GW_OK) {
        printf("could not bind the Sanxing driver\n");
        return 1;
    }

    for (int capture_index = 0; capture_index < CAPTURE_COUNT; capture_index++) {
        gw_reading_t readings[4];
        size_t count = 0;
        gw_result_t result = gw_decode(&session,
                                       CAPTURES[capture_index].res,
                                       CAPTURES[capture_index].res_len,
                                       readings, 4, &count);

        printf("%s\n", CAPTURES[capture_index].name);

        if (result != GW_OK) {
            printf("    <decode error: %s>\n", gw_result_to_string(result));
            continue;
        }
        for (size_t reading_index = 0; reading_index < count; reading_index++) {
            print_reading(&readings[reading_index]);
        }
    }
    return 0;
}
