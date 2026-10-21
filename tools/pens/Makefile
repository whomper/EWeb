# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# aerapens (aerapens.c): a picture made into an Atari screen's pens on the
# Pi's gateway. make builds both for this machine: build/aerapens-lj (as
# EWEB.PRG: libjpeg-turbo) and build/aerapens-tj (as EWEBST.PRG: TJpgDec);
# make pi builds them for a Pi, static (any Raspberry Pi OS's libc):
# build/arm64 and build/armhf. -fsigned-char: an ARM's char is unsigned, the
# 68000's signed; -fwrapv: TJpgDec's sums wrap, as on a 68000.
# ../../tests/pic: make pens checks them against the Atari's code.
CC     ?= cc
B      ?= build
S      := ../../src
T      := ../../third_party
CFLAGS := $(ARCH) -O2 -std=gnu99 -fsigned-char -fwrapv -Wall -Wextra -Wno-sign-compare -Wno-unused-parameter
INC    := -I$(S) -I$(T)/nsgif/include -Istub
COMMON := stub/plat.h $(S)/util.c aerapens.c $(S)/pic.c $(S)/png.c $(S)/inflate.c $(T)/nsgif/src/gif.c $(T)/nsgif/src/lzw.c
LJ     := $(T)/libjpeg-turbo
LJ_C   := jaricom jcomapi jdapimin jdapistd jdarith jdcoefct jdcolor jddctmgr jdhuff jdinput jdmainct jdmarker \
          jdmaster jdmerge jdphuff jdpostct jdsample jdtrans jidctfst jidctint jidctred jmemmgr jmemnobs \
          jquant1 jquant2 jutils
LJ_SRC := $(addprefix $(LJ)/src/,$(addsuffix .c,$(LJ_C)))
LJ_INC := -DPIC_LIBJPEG -DNO_GETENV -DNO_PUTENV -DAERA_NO_FLOAT -I$(LJ)/src -I$(LJ)/config
TJ_INC := -DNO_GETENV -DNO_PUTENV -DAERA_NO_FLOAT -I$(T)/tjpgd
HDR    := $(wildcard $(S)/*.h) $(wildcard $(LJ)/src/*.h) $(wildcard $(T)/tjpgd/*.h)

all: $(B)/aerapens-lj $(B)/aerapens-tj

# the third parties' warnings are theirs (-w), as the Atari builds have them
$(B)/aerapens-lj: $(COMMON) $(S)/pic_lj8.c $(LJ_SRC) $(HDR) | $(B)
	rm -rf $(B)/lj && mkdir -p $(B)/lj
	cd $(B)/lj && $(CC) $(CFLAGS) -w $(addprefix -I$(CURDIR)/,$(S) $(LJ)/src $(LJ)/config $(T)/nsgif/include) \
		-DPIC_LIBJPEG -DNO_GETENV -DNO_PUTENV -DAERA_NO_FLOAT -DNDEBUG -c $(addprefix $(CURDIR)/,$(LJ_SRC) \
		$(T)/nsgif/src/gif.c $(T)/nsgif/src/lzw.c)
	$(CC) $(CFLAGS) $(INC) $(LJ_INC) -o $@ aerapens.c $(S)/pic.c $(S)/png.c $(S)/inflate.c $(S)/util.c $(S)/pic_lj8.c $(B)/lj/*.o $(LDFLAGS)
$(B)/aerapens-tj: $(COMMON) $(S)/jpegdc.c $(T)/tjpgd/tjpgd.c $(HDR) | $(B)
	rm -rf $(B)/tj && mkdir -p $(B)/tj
	cd $(B)/tj && $(CC) $(CFLAGS) -w $(addprefix -I$(CURDIR)/,$(S) $(T)/tjpgd $(T)/nsgif/include) \
		-DNO_GETENV -DNO_PUTENV -DAERA_NO_FLOAT -DNDEBUG -c $(addprefix $(CURDIR)/,$(T)/tjpgd/tjpgd.c \
		$(T)/nsgif/src/gif.c $(T)/nsgif/src/lzw.c)
	$(CC) $(CFLAGS) $(INC) $(TJ_INC) -o $@ aerapens.c $(S)/pic.c $(S)/png.c $(S)/inflate.c $(S)/util.c $(S)/jpegdc.c $(B)/tj/*.o $(LDFLAGS)
pi:
	$(MAKE) CC=aarch64-linux-gnu-gcc B=build/arm64 LDFLAGS=-static
	$(MAKE) CC=arm-linux-gnueabihf-gcc B=build/armhf LDFLAGS=-static
$(B):
	mkdir -p $@
clean:
	rm -rf build
.PHONY: all pi clean
