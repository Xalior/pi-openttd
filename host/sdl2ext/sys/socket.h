//
// sys/socket.h — this board's sys/socket.h, with one socket option added.
//
// The C library here is newlib's Circle port. Its <sys/socket.h> defines the
// SO_ options POSIX lists, and SO_REUSEPORT is not one of them — it is a BSD
// extension that Linux and the BSDs both carry and POSIX does not.
//
// OpenTTD sets it in src/network/core/os_abstraction.cpp, so that a server
// and a client on the same machine can both bind the same port. The call is
// already written to carry on when it fails — SetReusePort returns false and
// the caller logs it — so what matters here is that the name exists for the
// call to be made.
//
// This is an option number a caller passes to setsockopt, not behaviour this
// port implements. What the C library does with it behind the call is the C
// library's business.
//
// The vendored header is left exactly as it is. This one sits ahead of it on
// the include path — see INCLUDE in the host Makefile — pulls it in through
// #include_next so everything it declares arrives unchanged, and then adds
// the one missing name. The value continues the vendored header's own
// numbering, which counts the SO_ options up from SO_ACCEPTCONN and ends at
// SO_TYPE, so the next offset is the first one free.
//
#ifndef _rapi_sys_socket_h
#define _rapi_sys_socket_h

#include_next <sys/socket.h>

#ifndef SO_REUSEPORT
#define SO_REUSEPORT (SO_ACCEPTCONN + 16)  // several sockets may bind one port
#endif

#endif
