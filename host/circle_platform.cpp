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
// The declarations they satisfy come from two places: this port's own
// headers in sdl2ext/, for the POSIX calls newlib's Circle port does not
// carry, and upstream's own library_loader.h, whose three private methods
// upstream implements once per platform. Excluding upstream's Unix
// implementation and writing this one is the whole of the change; the
// header, and every caller of it, is untouched.
//
#include <sys/ioctl.h>
#include <sys/utsname.h>
#include <ifaddrs.h>

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
