//
// circle_stubs.cpp — this port's own additions to the SDL2 surface layer
// OpenTTD needs.
//
// There are none left. This file used to carry two things: private
// definitions of surface, palette, blit and window-surface entry points
// circle-libsdl2 did not yet implement, and a --wrap around
// SDL_CreateRGBSurface and SDL_FreeSurface that added 8-bit paletted
// surfaces on top of a shim that only made 32-bit ones. circle-libsdl2 has
// since grown genuine implementations of all of it — paletted surfaces,
// palettes, blitting and the window-surface present path included — so both
// halves are gone, and the matching --wrap pair has been removed from
// host/Makefile with them.
//
// Nothing OpenTTD calls is known to be missing from circle-libsdl2. This
// file stays as the place a future gap would go: the archive is linked
// whole, so a stub added here for something the library already provides
// becomes a duplicate-symbol error at link time rather than a silent
// winner over the real thing.
//
