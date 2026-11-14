# SPDX-FileCopyrightText: 2026 The Chupathingy contributors
# SPDX-FileCopyrightText: 2026 Milenko x Horrible Dev
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The Delta reference client. `make` builds delta_ref.o and runs the tests;
# a port compiles delta_ref.c into its own build instead. Build output goes in
# BUILD (a folder of your choosing; build/ here by default).

CC ?= cc
BUILD ?= build
CFLAGS ?= -O2
WARNINGS = -std=c99 -pedantic -Wall -Wextra -Wconversion -Werror

all: test

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/delta_ref.o: delta_ref.c delta_ref.h | $(BUILD)
	$(CC) $(WARNINGS) $(CFLAGS) -c delta_ref.c -o $@

$(BUILD)/delta_ref_test: delta_ref.c delta_ref.h delta_ref_test.c golden.h | $(BUILD)
	$(CC) $(WARNINGS) -Wno-conversion $(CFLAGS) delta_ref.c delta_ref_test.c -o $@

test: $(BUILD)/delta_ref.o $(BUILD)/delta_ref_test
	$(BUILD)/delta_ref_test

clean:
	rm -rf $(BUILD)

.PHONY: all test clean
