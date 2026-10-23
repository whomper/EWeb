/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* EWEBJS.OVL (atari/js_ovl.h): as common/atari/link.ld, linked at 0, but
   with no crt0: the text starts with js_ovl_head, the calls into it.
   What it takes from EWEB.PRG is left undefined; tools/mkovl.py makes
   those its imports. */
ENTRY(js_ovl_head)
SECTIONS
{
  . = 0;
  .text : {
    KEEP(*(.ovlhead))
    *(.text .text.*)
    *(.rodata .rodata.*)
    . = ALIGN(4);
  }
  .data : {
    *(.data .data.*)
    . = ALIGN(4);
  }
  .bss : {
    *(.bss .bss.*)
    *(COMMON)
    . = ALIGN(4);
  }
  /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) *(.gnu*) }
}
