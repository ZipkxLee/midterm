CC ?= gcc
DEBUG ?= 1
BUILD_DIR := build
CPPFLAGS += -Iinclude -DDEBUG_BUILD=$(DEBUG)
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic -O2 -fPIC
LDFLAGS += -L$(BUILD_DIR) -Wl,-rpath,'$$ORIGIN'
LDLIBS += -lnetutil

.PHONY: all clean test demo capture

all: $(BUILD_DIR)/server $(BUILD_DIR)/client

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/netutil.o: src/netutil.c include/netutil.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/libnetutil.so: $(BUILD_DIR)/netutil.o
	$(CC) -shared -Wl,-soname,libnetutil.so -o $@ $^

$(BUILD_DIR)/server: src/server.c $(BUILD_DIR)/libnetutil.so
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(LDFLAGS) $(LDLIBS) -o $@

$(BUILD_DIR)/client: src/client.c $(BUILD_DIR)/libnetutil.so
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(LDFLAGS) $(LDLIBS) -o $@

test: all
	bash scripts/test.sh

demo: all
	bash scripts/demo.sh

capture: all
	bash scripts/capture.sh

clean:
	rm -rf $(BUILD_DIR)