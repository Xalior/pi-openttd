# sdl2ext — the POSIX headers this board's C library does not carry

OpenTTD includes headers, and expects constants in them, that exist on every
desktop Unix and that newlib's Circle port does not provide. They are
supplied here, in this port's own layer, and put first on the include path.

Nothing upstream is changed to find them: OpenTTD writes `#include
<ifaddrs.h>` and `#include <netdb.h>` exactly as it always has, and this
directory is what those resolve to.

There are two kinds of header in here, and the difference matters:

- **Headers the C library has none of at all** — `ifaddrs.h`,
  `sys/ioctl.h`, `sys/utsname.h`. Each declares only what OpenTTD actually
  uses, and says in its own comment what the bare-metal board can and cannot
  do behind it. The implementations are in `../circle_platform.cpp`.
- **Headers the C library does have, missing a constant OpenTTD needs** —
  `netdb.h`, `netinet/in.h`, `sys/socket.h`. These do not replace the C
  library's: each pulls the real one in through `#include_next` so
  everything it declares arrives unchanged, and then defines the one or two
  names that are not in it. The C library's own copy is never edited.

A constant added by the second kind is a number a caller passes in, not
behaviour this port implements. What the C library does with it behind the
call stays the C library's business.
