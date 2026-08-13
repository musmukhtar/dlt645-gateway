# Host build for the multi-meter gateway.
# Pure decoders over byte buffers -- no hardware needed.
#
#   make          build everything (test + demo)
#   make test     build and run the test suite
#   make run      build and run the demo (prints decoded readings)
#   make debug    build with debug symbols + sanitizers, run the tests
#   make clean    remove the built binaries
#
# Source layout is by LAYER, not by vendor:
#
#   include/gw/            public API -- all an integrator includes
#   src/core/          L4  brand-neutral: registry, bind, session
#   src/drivers/       L3  adapters, one gw_driver_t per brand
#   src/profiles/      L2  vendor data-item semantics
#   src/protocols/     L1  wire framing, reusable across vendors
#
# Includes are strictly downward: L1 never sees L2, L2 never sees L3.

CC       ?= gcc
CFLAGS   ?= -Wall -Wextra -std=c11 -O2
LIBS      = -lm

# Public API by its namespaced path (gw/...); the internal layers are on
# the search path so a driver can include its own protocol and profile.
INCLUDES  = -Iinclude \
            -Isrc/protocols/dlt645 \
            -Isrc/profiles/sanxing \
            -Itests

CORE_SRC      = src/core/gw_session.c
PROTOCOL_SRC  = src/protocols/dlt645/dlt645_frame.c
PROFILE_SRC   = src/profiles/sanxing/sanxing_profile.c
DRIVER_SRC    = src/drivers/sanxing_driver.c \
                src/drivers/conlog_driver.c

# The library: every layer.  Both binaries link this.
LIB_SRC = $(CORE_SRC) $(PROTOCOL_SRC) $(PROFILE_SRC) $(DRIVER_SRC)

TEST_BIN = test_sanxing
DEMO_BIN = demo

.PHONY: all test run debug clean

all: $(TEST_BIN) $(DEMO_BIN)

$(TEST_BIN): $(LIB_SRC) tests/test_sanxing.c
	$(CC) $(CFLAGS) $(INCLUDES) $(LIB_SRC) tests/test_sanxing.c $(LIBS) -o $@

$(DEMO_BIN): $(LIB_SRC) apps/demo.c
	$(CC) $(CFLAGS) $(INCLUDES) $(LIB_SRC) apps/demo.c $(LIBS) -o $@

test: $(TEST_BIN)
	./$(TEST_BIN)

run: $(DEMO_BIN)
	./$(DEMO_BIN)

# Debug build: symbols on, optimizer off, address/UB sanitizers on.
debug: CFLAGS = -Wall -Wextra -std=c11 -g -O0 -fsanitize=address,undefined
debug: clean $(TEST_BIN)
	./$(TEST_BIN)

clean:
	rm -f $(TEST_BIN) $(DEMO_BIN)
