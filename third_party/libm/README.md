# musl's maths functions in EWeb

The double-precision functions of musl's libm that QuickJS's `Math` and
number formatting need, for the Atari build of EWeb's scripts
(`third_party/quickjs`), under musl's MIT licence (`COPYRIGHT`). The hosts
use their own C library's.

| What | Where from |
|---|---|
| musl 1.2.5, `src/math` (the files in `src/`), `src/internal/libm.h`, `arch/generic/fp_arch.h` | https://git.musl-libc.org/cgit/musl (the mirror https://github.com/kraj/musl), tag `v1.2.5`, commit `0784374d561435f7c787a555aeab8ede699ed298` |

The files in `src/` are musl's as they are. EWeb's own, in `eweb/`
(GPL-2.0-or-later, as the rest of EWeb):

- `math.h`, `fenv.h`, `endian.h`: what the functions and QuickJS include,
  for a freestanding 68030 with an FPU (round to nearest, no exception
  flags, big-endian);
- `features.h`: musl's `hidden` and `weak_alias`, given to each file with
  `-include features.h`;
- `eweb_fpu.c`: `sqrtl`, which `hypot` uses in long double: the FPU's own
  square root.

They are built with `-m68881` (the Makefile's `TOSJS_CFLAGS`).
