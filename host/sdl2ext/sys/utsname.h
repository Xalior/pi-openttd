//
// sys/utsname.h — what machine this is, for a machine that has no uname.
//
// OpenTTD asks twice: its crash handler puts the answer in a crash report,
// and its survey code puts it in the optional telemetry it can send. Every
// desktop Unix has this header; newlib's Circle port does not, so this port
// supplies the declaration and an implementation (in ../circle_platform.cpp)
// that answers with what the board actually is.
//
// The field widths are the usual POSIX ones. Nothing here reads them as
// anything but strings.
//
#ifndef _rapi_sys_utsname_h
#define _rapi_sys_utsname_h

#define _UTSNAME_LENGTH 65

#ifdef __cplusplus
extern "C" {
#endif

struct utsname
{
    char sysname[_UTSNAME_LENGTH];      // the system: Circle
    char nodename[_UTSNAME_LENGTH];     // the machine's name on a network
    char release[_UTSNAME_LENGTH];      // the system's release
    char version[_UTSNAME_LENGTH];      // the build of that release
    char machine[_UTSNAME_LENGTH];      // the processor architecture
};

int uname(struct utsname *buf);

#ifdef __cplusplus
}
#endif

#endif
