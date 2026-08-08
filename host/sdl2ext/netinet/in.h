//
// netinet/in.h — this board's netinet/in.h, with one socket option added.
//
// The C library here is newlib's Circle port. Its <netinet/in.h> says of its
// own IPv6 section that the definitions are "not really supported, but are
// necessary to compile", and it stops at the multicast group options.
// IPV6_V6ONLY is not among them.
//
// OpenTTD sets it in src/network/core/address.cpp, on an AF_INET6 listening
// socket, to keep that socket to IPv6 alone rather than accepting IPv4
// connections mapped into it. The call is already written to carry on when
// it fails — it logs and continues — so what matters here is that the name
// exists for the call to be made.
//
// This is an option number a caller passes to setsockopt, not behaviour this
// port implements. What the C library does with it behind the call is the C
// library's business.
//
// The vendored header is left exactly as it is. This one sits ahead of it on
// the include path — see INCLUDE in the host Makefile — pulls it in through
// #include_next so everything it declares arrives unchanged, and then adds
// the one missing name.
//
// The value continues the vendored header's own numbering rather than
// borrowing Linux's. That header numbers every IP and IPV6 option as an
// offset from IP_ADD_MEMBERSHIP, ending at IPV6_LEAVE_GROUP, so the next
// offset is the first one free. Linux's value for IPV6_V6ONLY is 26, which
// in that scheme is already IP_RETOPTS: legal, because the two are read at
// different socket levels, but indistinguishable to anyone reading the
// numbers.
//
#ifndef _rapi_netinet_in_h
#define _rapi_netinet_in_h

#include_next <netinet/in.h>

#ifndef IPV6_V6ONLY
#define IPV6_V6ONLY (IP_ADD_MEMBERSHIP + 38)  // this socket is IPv6 only
#endif

#endif
