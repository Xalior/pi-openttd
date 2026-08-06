//
// sys/ioctl.h — the socket control call, for the one use OpenTTD makes of it.
//
// OpenTTD calls ioctl(socket, FIONBIO, &on) to put a socket into
// non-blocking mode, and nothing else in the program uses this header.
// newlib's Circle port has no <sys/ioctl.h>, so this port supplies the
// declaration and an implementation (in ../circle_platform.cpp).
//
// FIONREAD is declared beside it because the two are always defined
// together and a reader looking for one expects to find the other.
//
#ifndef _rapi_sys_ioctl_h
#define _rapi_sys_ioctl_h

#ifndef FIONREAD
#define FIONREAD  0x541B        // bytes readable without blocking
#endif
#ifndef FIONBIO
#define FIONBIO   0x5421        // set or clear non-blocking mode
#endif

#ifdef __cplusplus
extern "C" {
#endif

int ioctl(int fd, unsigned long request, ...);

#ifdef __cplusplus
}
#endif

#endif
