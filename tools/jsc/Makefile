# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# aerajsc, webgate's compiler of scripts for EWeb (aerajsc.c), on its own
# (AERA-Gateway builds it on the Pi from its jsc/ copy of this folder and
# EWeb's third_party/quickjs; EWeb's own Makefile builds build/host/aerajsc):
#
#   make -C tools/jsc        build/aerajsc
#
# EWeb's QuickJS with EWeb's patches, as EWEB.PRG has it: CONFIG_EWEB (the
# bytecode big-endian, its tag) and the same version, so that what it
# makes is what EWEB.PRG would (EWeb's make bc68k). QuickJS is built as
# EWeb builds it, without its warnings (-w).
CC     ?= cc
QJS    ?= ../../third_party/quickjs
QJS_C  := quickjs libregexp libunicode cutils dtoa
CFLAGS ?= -O2
# (QuickJS's sources summed into its bytecode's tag, as EWeb's Makefile does)
SRCS   := $(sort $(wildcard $(QJS)/*.c $(QJS)/*.h))
SUM    := $(shell cat $(SRCS) | sha256sum | cut -c1-8)
DEF    := -DCONFIG_EWEB -DCONFIG_VERSION=\"$(shell cat $(QJS)/VERSION)\" -DCONFIG_EWEB_SUM=\"$(SUM)\"
OBJ    := $(addprefix build/,$(addsuffix .o,$(QJS_C)))

build/aerajsc: aerajsc.c $(OBJ)
	$(CC) $(CFLAGS) -std=gnu99 -Wall -Wextra -Wno-unused-parameter -I$(QJS) -o $@ $^ -lm
build/quickjs.o: $(SRCS)
build/%.o: $(QJS)/%.c $(wildcard $(QJS)/*.h)
	@mkdir -p build
	$(CC) $(CFLAGS) -std=gnu99 $(DEF) -w -c $< -o $@
clean:
	rm -rf build
.PHONY: clean
