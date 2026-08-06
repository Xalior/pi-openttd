# sdl2ext — the POSIX headers this board's C library does not carry

OpenTTD includes a handful of headers that exist on every desktop Unix and
that newlib's Circle port does not ship. They are supplied here, in this
port's own layer, and put first on the include path.

Nothing upstream is changed to find them: OpenTTD writes `#include
<ifaddrs.h>` exactly as it always has, and this directory is what that
resolves to.

Each header declares only what OpenTTD actually uses, and each one says in
its own comment what the bare-metal board can and cannot do behind it. The
implementations are in `../circle_platform.cpp`.
