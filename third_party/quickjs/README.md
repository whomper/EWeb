# QuickJS in EWeb

Fabrice Bellard's and Charlie Gordon's QuickJS, the JavaScript engine
EWeb's scripts run on (`src/js`, docs/DESIGN.md: "A little JavaScript"),
under the MIT licence (`LICENSE`).

| What | Where from |
|---|---|
| QuickJS 2026-06-04 (`VERSION`) | https://github.com/bellard/quickjs, commit `84e723f1ace5e3823cef8149dd0f890fe74b77ce` (13 Oct 2026) |

Only the engine was taken: `quickjs.c` and `.h`, its atom and opcode
tables, `libregexp`, `libunicode` (and its tables), `cutils`, `dtoa`,
`list.h`, `LICENSE`, `VERSION` and `readme.txt`. Not its C library
(`quickjs-libc.c`: files, processes, `os` and `std`), its command line
programs, tests or Makefile. EWeb's `Makefile` builds it with
`-DCONFIG_EWEB -DCONFIG_VERSION="2026-06-04"`, without Intl and Temporal
(not added to EWeb's contexts).

On the Atari it is built for the FPU (`-m68030 -m68881`): JavaScript's
numbers are all doubles, and libgcc's soft-float doubles are
position-independent code that EWeb's freestanding link can't take.
`EWEB.PRG` runs scripts only when the `_FPU` cookie says there is one;
the ST edition has none of it (`src/js/js_none.c`). The maths functions
are musl's (`third_party/libm`), the few C library calls the system
hasn't are `atari/js_libc.c` with the headers in `atari/include/js`.

## Local changes

Each is in `patches/` as a patch against the upstream commit (git am
applies it), so that an update can carry it again. EWeb takes no part in
QuickJS's development and doesn't send them upstream.

| Patch | Why |
|---|---|
| `quickjs-0001-eweb-freestanding.patch` | The Atari build has no operating system's C library: with `CONFIG_EWEB`, no `malloc.h` (`alloca` is the compiler's), `malloc_usable_size` answers 0 (as for Emscripten) and there are no Atomics (no threads). |
