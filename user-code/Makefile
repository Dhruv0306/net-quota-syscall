# User-space side of net_quota: wrapper library, CLI, test programs.
#
#   make            build everything into ./build
#   make test       run all tests (root, on the net-quota kernel)
#   make clean
#
# The NQ_* codes come from the kernel's UAPI header. By default it is taken
# from ../kernel-code; override KERNEL_UAPI to use another copy.

CC       ?= gcc
CFLAGS   ?= -O2 -g
CFLAGS   += -std=gnu11 -Wall -Wextra -Wshadow
KERNEL_UAPI ?= ../kernel-code/new-files/include/uapi
CPPFLAGS += -Iinclude -Itests -I$(KERNEL_UAPI)

BUILD := build
LIB   := $(BUILD)/net_quota.o
COMMON := $(BUILD)/test_common.o
TESTS := $(BUILD)/test_basic $(BUILD)/test_errors $(BUILD)/test_inherit
BINS  := $(BUILD)/nq_cli $(BUILD)/net_server $(TESTS)

all: $(BINS)

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/%.o: src/%.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: tests/%.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD)/nq_cli: $(BUILD)/nq_cli.o $(LIB)
	$(CC) $(LDFLAGS) $^ -o $@

$(BUILD)/net_server: $(BUILD)/net_server.o
	$(CC) $(LDFLAGS) $^ -o $@

$(BUILD)/test_%: $(BUILD)/test_%.o $(COMMON) $(LIB)
	$(CC) $(LDFLAGS) $^ -o $@

test: all
	sudo ./tests/run_tests.sh

clean:
	rm -rf $(BUILD)

.PHONY: all test clean
.SECONDARY:
