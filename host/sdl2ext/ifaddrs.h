//
// ifaddrs.h — the interface-address list, for a board that has none to list.
//
// OpenTTD walks this list once, to work out which broadcast addresses it
// could send a LAN server announcement to. Every desktop Unix has it;
// newlib's Circle port does not, so this port supplies the declaration and
// an implementation (in ../circle_platform.cpp) that honestly reports an
// empty list.
//
// The structure is the BSD one, field for field, because that is the shape
// the code walking it expects. IFF_BROADCAST is declared here as well: the
// flag lives in <net/if.h> on a desktop, this board's <net/if.h> is empty,
// and the only code that reads it is the same loop that reads ifa_flags.
//
#ifndef _rapi_ifaddrs_h
#define _rapi_ifaddrs_h

#include <sys/socket.h>

#ifndef IFF_BROADCAST
#define IFF_BROADCAST 0x2       // the interface can broadcast
#endif

#ifdef __cplusplus
extern "C" {
#endif

struct ifaddrs
{
    struct ifaddrs  *ifa_next;
    char            *ifa_name;
    unsigned int     ifa_flags;
    struct sockaddr *ifa_addr;
    struct sockaddr *ifa_netmask;
    struct sockaddr *ifa_broadaddr;
    void            *ifa_data;
};

// Answers "no interfaces": success, with an empty list. A failure return
// would be read as "the question could not be asked", which is a different
// and less true statement — the question was asked and the answer is none.
int  getifaddrs(struct ifaddrs **ifap);
void freeifaddrs(struct ifaddrs *ifa);

#ifdef __cplusplus
}
#endif

#endif
