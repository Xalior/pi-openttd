//
// netdb.h — this board's netdb.h, with two name-resolution flags put back.
//
// The C library here is newlib's Circle port, and its <netdb.h> carries a
// deliberately small subset of the name-resolution interface: getaddrinfo,
// getnameinfo and their struct, with only some of the flag constants those
// two functions take. NI_NUMERICHOST is present in that header as a
// commented-out line; AI_ADDRCONFIG is not there at all.
//
// OpenTTD's networking uses both, in src/network/core/address.cpp:
//
//   NI_NUMERICHOST   asks getnameinfo for the address in numeric form
//                    rather than a name, which is what a board with no
//                    resolver can actually answer.
//   AI_ADDRCONFIG    asks getaddrinfo to return only families the machine
//                    has an address in, which is a hint and may be ignored.
//
// Both are flag words a caller passes in, not behaviour this port
// implements. Defining them lets the call compile and be made; what the C
// library does with them behind the call is the C library's business, and
// on this board that is as much or as little as its own implementation
// chooses.
//
// The vendored header is left exactly as it is. This one sits ahead of it
// on the include path — see INCLUDE in the host Makefile — pulls it in
// through #include_next so everything it declares arrives unchanged, and
// then adds the two missing names. Upstream is not modified to find it:
// OpenTTD writes #include <netdb.h> as it always has.
//
// The values follow the numbering the vendored header itself uses, which is
// glibc's: NI_NUMERICHOST is the value on its own commented-out line, and
// AI_ADDRCONFIG continues the AI_ series past AI_NUMERICHOST without
// colliding with any flag already defined there.
//
#ifndef _rapi_netdb_h
#define _rapi_netdb_h

#include_next <netdb.h>

#ifndef NI_NUMERICHOST
#define NI_NUMERICHOST  1       // return the address numerically, not as a name
#endif

#ifndef AI_ADDRCONFIG
#define AI_ADDRCONFIG   0x0020  // only families this machine has an address in
#endif

#endif
