//
// circle_platform.cpp — the machine facilities OpenTTD expects an operating
// system to have, answered for a board that has no operating system.
//
// Each of these is a place where upstream calls something every desktop
// Unix provides and this board does not. None of them is faked: each does
// the real thing where the board can, and where it cannot it returns the
// honest answer — no interfaces, no shared libraries — rather than a
// plausible one.
//
// The declarations they satisfy come from three places:
//
//   * this port's own headers in sdl2ext/, for the POSIX calls newlib's
//     Circle port has no header for at all;
//   * newlib's own headers, for the calls it declares and then does not
//     build — its <unistd.h>, <pwd.h>, <signal.h>, <sys/wait.h>, <stdio.h>
//     and <netdb.h> all promise functions that are in no library shipped
//     with it, and the link is where that promise comes due;
//   * upstream's own library_loader.h, whose three private methods upstream
//     implements once per platform. Excluding upstream's Unix implementation
//     and writing this one is the whole of the change; the header, and every
//     caller of it, is untouched.
//
#include <sys/ioctl.h>
#include <sys/utsname.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pwd.h>
#include <signal.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <type_traits>

#include <circle/version.h>

#include <SDL2/SDL_circle.h>

#include "../openttd/src/library_loader.h"

static const char From[] = "openttd-platform";

extern "C" {

// ---------------------------------------------------------------------------
// ioctl
// ---------------------------------------------------------------------------
//
// OpenTTD makes exactly one ioctl call: put a socket into non-blocking mode
// before connecting. Circle's socket layer has no such control call, and a
// socket here is either usable or it is not, so the request is refused
// rather than silently reported as done — a caller told its socket is
// non-blocking when it is not would then block the whole game.
//
// Nothing else in the program calls ioctl, so nothing else is answered.
int ioctl(int fd, unsigned long request, ...)
{
    (void)fd;
    (void)request;
    errno = ENOSYS;
    return -1;
}

// ---------------------------------------------------------------------------
// getifaddrs
// ---------------------------------------------------------------------------
//
// OpenTTD walks the interface list once, to collect the broadcast addresses
// it could announce a LAN game on. This board's network stack is never
// started — see the README — so the list is empty, and an empty list is
// what is returned: success, with nothing in it. Returning failure instead
// would say the question could not be asked, which is a different claim.
int getifaddrs(struct ifaddrs **ifap)
{
    if (ifap == nullptr)
    {
        errno = EINVAL;
        return -1;
    }
    *ifap = nullptr;
    return 0;
}

void freeifaddrs(struct ifaddrs *ifa)
{
    (void)ifa;      // nothing was ever allocated
}

// ---------------------------------------------------------------------------
// uname
// ---------------------------------------------------------------------------
//
// OpenTTD asks what machine it is running on twice: its crash handler puts
// the answer in a crash report, and its survey code puts it in the optional
// telemetry. The answer here is the plain truth — the framework the board is
// running, the board model it was built for, and the architecture — rather
// than an imitation of a Linux that is not there. A crash report that said
// "Linux" would send somebody looking for a kernel log that does not exist.
//
// There is no name service and no network identity, so the node name is the
// same on every board.
static void CopyField(char *dst, const char *src)
{
    strncpy(dst, src, _UTSNAME_LENGTH - 1);
    dst[_UTSNAME_LENGTH - 1] = '\0';
}

int uname(struct utsname *buf)
{
    if (buf == nullptr)
    {
        errno = EFAULT;
        return -1;
    }

    CopyField(buf->sysname,  CIRCLE_NAME);
    CopyField(buf->nodename, "raspberrypi");
    CopyField(buf->release,  CIRCLE_VERSION_STRING);
    CopyField(buf->machine,  "aarch64");

    // The board this image was built for. RASPPI is set by the world's own
    // configuration and is the only thing here that differs between the
    // three images.
    snprintf(buf->version, _UTSNAME_LENGTH, "bare metal, Raspberry Pi %d",
             (int)RASPPI);
    return 0;
}

// ---------------------------------------------------------------------------
// getuid, getpwuid
// ---------------------------------------------------------------------------
//
// OpenTTD asks who is running it in one place: it looks for the user's home
// directory to keep saved games and configuration in, and falls back to
// $HOME's absence by asking the password database.
//
// There is no password database here and there are no users — the game owns
// the machine — so the answer is user 0 and no database entry. OpenTTD reads
// that as "no home directory", which is true, and uses the directory the
// kernel already put it in (RAPI_GAME_DIR) instead.
uid_t getuid(void)
{
    return 0;
}

struct passwd *getpwuid(uid_t uid)
{
    (void)uid;
    errno = ENOENT;
    return nullptr;     // no password database on this board
}

// ---------------------------------------------------------------------------
// flockfile, funlockfile
// ---------------------------------------------------------------------------
//
// The bundled {fmt} brackets a run of character writes with these so that
// another thread printing at the same moment cannot interleave with it.
// newlib declares the pair in <stdio.h> and this build of it defines
// neither, and its FILE has no lock for them to take: a stream here is
// written straight through.
//
// So they do nothing, and doing nothing is the whole of what they can do.
// The bracket keeps its meaning for the code that writes it, and a caller
// that expected the writes to be indivisible does not get that guarantee —
// on this board it was never on offer.
void flockfile(FILE *file)
{
    (void)file;
}

void funlockfile(FILE *file)
{
    (void)file;
}

// ---------------------------------------------------------------------------
// execvp, waitpid
// ---------------------------------------------------------------------------
//
// Two places in OpenTTD start another program: the external-player music
// backend hands a MIDI file to whatever the player is set to, and "open in
// browser" hands a URL to the desktop. Both then wait for the child.
//
// There are no processes on this board — there is one program and it is this
// one — so neither can be done, and both say so rather than reporting a
// child that was never started. execvp does not return at all when it
// succeeds, so its only possible return is this failure.
int execvp(const char *file, char *const argv[])
{
    (void)file;
    (void)argv;
    errno = ENOSYS;
    return -1;
}

pid_t waitpid(pid_t pid, int *status, int options)
{
    (void)pid;
    (void)options;
    if (status != nullptr) *status = 0;
    errno = ECHILD;     // nothing was ever started, so there is nothing to wait for
    return -1;
}

// ---------------------------------------------------------------------------
// sigaction, sigprocmask
// ---------------------------------------------------------------------------
//
// OpenTTD's crash handler installs handlers for the fatal signals and
// unblocks them again while it writes the report. Circle raises no signals:
// a fault on this board is a Circle exception handler and a halt, and no
// POSIX handler is ever reached.
//
// Both therefore refuse, which is what a caller needs to know. OpenTTD
// ignores the return in both places, and the code that would have run in a
// handler is unreachable rather than silently disarmed — nothing pretends a
// handler is installed.
int sigaction(int signum, const struct sigaction *act, struct sigaction *oldact)
{
    (void)signum;
    (void)act;
    if (oldact != nullptr) memset(oldact, 0, sizeof(*oldact));
    errno = ENOSYS;
    return -1;
}

int sigprocmask(int how, const sigset_t *set, sigset_t *oldset)
{
    (void)how;
    (void)set;
    if (oldset != nullptr) memset(oldset, 0, sizeof(*oldset));
    errno = ENOSYS;
    return -1;
}

// ---------------------------------------------------------------------------
// getnameinfo
// ---------------------------------------------------------------------------
//
// newlib's Circle port declares getnameinfo in <netdb.h> and ships no
// implementation of it. OpenTTD calls it in exactly one place, to turn a
// socket address it already holds into the string it shows and logs, and it
// asks for the numeric form.
//
// That is a formatting job, not a lookup: the address is in the structure
// the caller passed in, and this fills in the text. There is no resolver on
// this board and no name to find, so the numeric form is not merely what was
// asked for — it is the only answer there is, and a request for a name gets
// the number too rather than an error.
//
// The node buffer is ALWAYS left holding a NUL-terminated string, on every
// path including the failing ones. OpenTTD's caller does not check the
// return value: it declares a buffer on the stack, calls this, and builds a
// std::string from it. A failure that left the buffer alone would be read as
// whatever was on the stack.
static void FormatIPv4(char *node, socklen_t nodelen, const struct in_addr *addr)
{
    const unsigned char *b = (const unsigned char *)&addr->s_addr;
    snprintf(node, nodelen, "%u.%u.%u.%u",
             (unsigned)b[0], (unsigned)b[1], (unsigned)b[2], (unsigned)b[3]);
}

// Written out in full, group by group, rather than in the shortened form
// RFC 5952 prefers. Nothing here parses the result back, and the long form
// is unambiguous.
static void FormatIPv6(char *node, socklen_t nodelen, const struct in6_addr *addr)
{
    const unsigned char *b = addr->s6_addr;
    snprintf(node, nodelen,
             "%x:%x:%x:%x:%x:%x:%x:%x",
             (b[0]  << 8) | b[1],  (b[2]  << 8) | b[3],
             (b[4]  << 8) | b[5],  (b[6]  << 8) | b[7],
             (b[8]  << 8) | b[9],  (b[10] << 8) | b[11],
             (b[12] << 8) | b[13], (b[14] << 8) | b[15]);
}

int getnameinfo(const struct sockaddr *sa, socklen_t salen,
                char *node, socklen_t nodelen,
                char *service, socklen_t servicelen,
                int flags)
{
    (void)flags;        // the answer is numeric whatever is asked for

    if (node != nullptr && nodelen > 0) node[0] = '\0';
    if (service != nullptr && servicelen > 0) service[0] = '\0';

    if (sa == nullptr) return EAI_FAMILY;

    unsigned short port = 0;

    switch (sa->sa_family)
    {
    case AF_INET:
    {
        if (salen < (socklen_t)sizeof(struct sockaddr_in)) return EAI_FAMILY;
        const struct sockaddr_in *in = (const struct sockaddr_in *)sa;
        if (node != nullptr && nodelen > 0) FormatIPv4(node, nodelen, &in->sin_addr);
        port = ntohs(in->sin_port);
        break;
    }

    case AF_INET6:
    {
        if (salen < (socklen_t)sizeof(struct sockaddr_in6)) return EAI_FAMILY;
        const struct sockaddr_in6 *in6 = (const struct sockaddr_in6 *)sa;
        if (node != nullptr && nodelen > 0) FormatIPv6(node, nodelen, &in6->sin6_addr);
        port = ntohs(in6->sin6_port);
        break;
    }

    default:
        return EAI_FAMILY;
    }

    if (service != nullptr && servicelen > 0)
    {
        snprintf(service, servicelen, "%u", (unsigned)port);
    }
    return 0;
}

} // extern "C"

// ---------------------------------------------------------------------------
// LibraryLoader
// ---------------------------------------------------------------------------
//
// OpenTTD loads shared libraries for one feature: the social-integration
// plugins, which report what the player is doing to a chat or storefront
// program running beside the game. Every part of that is absent here —
// there is no dynamic loader, no second process and no network — so this
// implementation reports that a library cannot be opened, and the plugin
// scan finds nothing and moves on. That is the same path a desktop takes
// when no plugin is installed.

void *LibraryLoader::OpenLibrary(const std::string &filename)
{
    this->error = "loading a shared library is not possible on this platform: "
                  + filename;
    SDL2Circle_Log(From, SDL2CIRCLE_LOG_NOTICE,
                   "shared library \"%s\" not loaded — there is no dynamic "
                   "loader on this board", filename.c_str());
    return nullptr;
}

void LibraryLoader::CloseLibrary()
{
    // Nothing is ever opened, so nothing is ever closed. The destructor
    // only reaches this when the handle is not null, which cannot happen.
}

void *LibraryLoader::GetSymbol(const std::string &symbol_name)
{
    this->error = "no library is loaded, so no symbol can be found: "
                  + symbol_name;
    return nullptr;
}
