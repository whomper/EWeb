EWeb's measuring tools for FireBee owners
==========================================

A test, not a FireBee edition yet. EWeb runs on the FireBee today through
FireTOS's 68k emulation (cf68klib), slowly; these two tools measure how
slowly, so that the FireBee edition, built for the ColdFire itself, has
numbers to beat.

Please run both, under FireTOS (with or without MiNT):

1. NSBENCH.TTP: double-click it. It reads, styles and lays out a news
   page (news.htm, news.css) three times, as EWeb does. A minute or two.
   It needs no network.

2. EWGET.TTP: double-click it, with your network (STinG or MiNTnet) up.
   It fetches https://www.example.com/ twice: the first time with a full
   secure handshake, the second over the same connection. A minute or
   two. It needs TLS.OVL, CACERT.PEM and ROOTS.DAT, which are in this
   folder.

Each writes a log next to itself: NSBENCH.LOG and EWGET.LOG. Their first
lines describe the machine (its cookies, TOS version, MiNT); a line that
says "no answer" means the machine refused that question, which is fine.
If a tool stops with bombs, please send the log anyway: it says how far
it got.

Please also try EWEB.PRG (in the folder above this one): does it start,
and does it show a page?

Send both logs, and what EWEB.PRG did, to Erez (or as he asked you to).
Thank you.

EWeb is free software (GPL 2 or later). Copyright (C) 2026 AERA | Erez Yaary.
