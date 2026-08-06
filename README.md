# pi-openttd

**OpenTTD running directly on a Raspberry Pi with no operating system.** The
board powers on and the game is what boots: no Linux, no desktop, no
launcher, and nothing else running beside it.

It builds for the Raspberry Pi 3, Pi 4 and Pi 5, all three from one source
tree.

## What this is

[OpenTTD](https://github.com/OpenTTD/OpenTTD) is an open source
reimplementation of Transport Tycoon Deluxe: a large, modern C++ application
with a simulation running underneath a mouse-driven interface. This
repository is the thin layer that lets it run with nothing underneath: a
[Circle](https://github.com/rsta2/circle) kernel that brings the board up,
and [circle-libsdl2](https://github.com/Xalior/circle-libsdl2), an SDL2
implementation built on Circle's bare-metal drivers.

The game's own source is not copied or modified here. It is a submodule,
pinned at an upstream commit, and the build reads it without ever writing to
it. Where the game needs something the SDL2 layer does not provide, this
repository supplies it in `host/` rather than changing the game.

Three processor cores are given separate work:

- **Core 0** owns the hardware. Circle's world lives here — interrupts, USB,
  the SD card, sound — and no other core touches a device.
- **Core 1** runs the game and nothing else.
- **Core 2** puts finished frames on the screen. The game draws into a
  640x480 window and never learns the display's real size; the picture is
  scaled once, at the end, onto whatever the screen is really showing.

## State of this port

This is an early port. Read the next two sections before expecting anything
of it. The list below describes what the code does, not what has been seen
to happen.

### What is missing, and what it costs

Several libraries OpenTTD normally uses do not exist on a bare-metal board.
Upstream already supports being built without each of them, so nothing here
pretends to provide one. What each absence costs:

- **No zlib, no LZMA, no LZO.** Saved games are written and read
  uncompressed. OpenTTD always carries that format, so saving and loading
  work; the files are simply larger. A saved game compressed on a desktop
  cannot be read here.
- **No libpng.** Screenshots are written as BMP or PCX instead of PNG.
- **No libcurl.** There is no HTTP client, so the in-game content download
  service cannot be reached. Every graphics, sound and music set has to be
  put on the card by hand.
- **No FreeType, no Fontconfig, no ICU, no HarfBuzz.** Text is drawn with
  OpenTTD's own built-in sprite font. Languages that need complex text
  shaping — Arabic, Hebrew, the Indic scripts — will not be laid out
  correctly.
- **No music.** The only music backend that could be compiled in plays a MIDI
  file by starting a separate program, and there is no second process to
  start. Sound effects are unaffected and use the SDL2 backend.
- **No network.** Circle has a network stack, but this kernel never starts
  it, so multiplayer and the server list are unavailable. The network code is
  still compiled, and reports that it cannot start.
- **No OpenGL.** The software blitters are the only ones built.

### What has and has not been tried

The port compiles and links completely for all three boards. **It has not
been run on hardware.** Nothing below has been observed; each is a statement
about the code.

- **The mouse.** OpenTTD is driven almost entirely by the mouse, and
  circle-libsdl2 implements the whole SDL mouse interface over Circle's USB
  input. It has never been exercised by a program that depends on it. This is
  the single most likely thing to need work.
- **Threads.** OpenTTD uses `std::thread` for saving games and for generating
  the world in the background. The C++ library here provides threads over
  Circle's cooperative scheduler, which runs on core 0, while the game runs
  on core 1. Whether that arrangement holds under load is exactly what this
  port is for.
- **Long runs.** OpenTTD is a program people leave running for hours. Nothing
  here has run for more than the length of a build.

## Building

You need:

- The **Arm GNU toolchain** for the `aarch64-none-elf` target, release
  15.2.Rel1. Put its `bin` directory on your `PATH`, or unpack it into
  `toolchains/`, or set `RAPI_TOOLCHAIN_DIR` to where it lives. See
  `mk/toolchain.mk`.
- **CMake**. OpenTTD generates some of its own sources — the string
  identifiers, the settings table, the script bindings, the revision file —
  with programs that have to run on the machine doing the building. Rather
  than reimplement any of that, this build runs upstream's own CMake once to
  produce them. That is the only thing CMake is used for; the cross build
  itself is plain `make`.
- On **macOS**, GNU `getopt` and a version 5 `bash` from Homebrew. The
  system's own are too old for the dependency build's configure script. The
  makefiles find them if they are installed and say nothing if they are not
  needed.

Then:

    git clone --recursive https://github.com/Xalior/pi-openttd.git
    cd pi-openttd
    make deps          # long: builds newlib and libc++ from source, three times
    make kernels       # all three boards
    make verify        # checks the images exist and are not empty

`make deps` is the expensive step. It configures and builds a separate
[circle-stdlib](https://github.com/smuehlst/circle-stdlib) world for each
board, because the processor, and therefore the C library, differs between
them. Expect it to take a long time on the first run and no time afterwards.
On a machine that cannot hold three worlds at once, `make deps-rpi5` builds
one.

`make rpi3`, `make rpi4` and `make rpi5` build one board each.

The C++ standard used is C++23, not the C++20 upstream asks for. The reason
is the standard library, not the game: the libc++ in these worlds uses a
C++23 construct in its own headers, and GCC rejects it at C++20.

## Game data, and `make media`

**This repository ships no game data, and `make card` never downloads
anything.** OpenTTD needs a graphics set to start at all, and wants a sound
set and a music set.

Two directories, and the difference between them matters:

| | |
|---|---|
| `media/` | Where game data lives on your machine. `make media` downloads into it; you copy your own files into it by hand. It is never committed and never shipped. |
| `build/sd-card/` | What `make card` stages. It copies from `media/` and fetches nothing. |

`make card` works whether or not `media/` has anything in it. A card built
with no data is a real card — it just says plainly which files are missing.

### `make media` — what it downloads

    make media

It downloads three files, with `curl`, all published by the OpenTTD project
at [cdn.openttd.org](https://cdn.openttd.org/):

- **OpenGFX**, the free graphics set — `opengfx-8.0.tar`. OpenTTD refuses to
  start without a graphics set, and this is it.
  `https://cdn.openttd.org/opengfx-releases/8.0/opengfx-8.0-all.zip`.
  Licence: GNU GPL v2.
- **OpenSFX**, the free sound effects set — `opensfx-1.0.3.tar`. The game
  starts without it; it just plays no sound effects.
  `https://cdn.openttd.org/opensfx-releases/1.0.3/opensfx-1.0.3-all.zip`.
  Licence: Creative Commons Attribution-ShareAlike 3.0 Unported.
- **OpenMSX**, the free music set — `openmsx-0.4.2.tar`. The game starts
  without it; it just plays no music, and this port's own music backend is
  disabled regardless — see "No music" above.
  `https://cdn.openttd.org/openmsx-releases/0.4.2/openmsx-0.4.2-all.zip`.
  Licence: GNU GPL v2.

Each set is published as a `.zip` holding one `.tar`. The zip is checked
against the SHA256 published in the release's own `manifest.yaml` on the
same host, then unpacked — OpenTTD reads a `.tar` directly and does not need
it extracted further, so the `.tar` lands in `media/` exactly as unpacked,
checked again against the SHA256 this project computed from it. A
`provenance.txt` is written beside them recording the URLs, the date, the
licences and both sets of checksums. Running it again re-verifies what is
already there instead of downloading it a second time.

### What `make media` will not fetch

**The original Transport Tycoon Deluxe data files**, if you own a copy.
OpenTTD reads them directly. They are a commercial product and are not
distributed by anyone, so nothing here goes looking for them — copy them by
hand, unchanged, into `games/openttd/baseset/` on the staged card, beside the
files `make card` already puts there.

## Putting it on a card

    make card

stages everything into `build/sd-card/`. Copy the contents to a FAT32 card,
then add the one thing the build cannot provide:

- **The Raspberry Pi firmware files** — `bootcode.bin`, `start*.elf`,
  `fixup*.dat` and, for the Pi 4, `armstub8-rpi4.bin` — at the root of the
  card. They come from the
  [Raspberry Pi firmware repository](https://github.com/raspberrypi/firmware).

If `make media` was not run first, `games/openttd/baseset/` is missing the
graphics, sound and music sets, and `make card` says so plainly.

The card ends up looking like this:

    kernel8.img            the Pi 3 image
    kernel8-rpi4.img       the Pi 4 image
    kernel_2712.img        the Pi 5 image
    config.txt             firmware boot configuration, all three boards
    cmdline.txt            kernel options
    games/openttd/         everything the game reads and writes

Every file the game touches is under `games/openttd/`, and nothing of it is
written to the root of the card. That is deliberate: a card can carry several
games, and two of them writing a settings file to the root would each
silently overwrite the other's.

### The thermal settings in `cmdline.txt`

`socmaxtemp=70` is the temperature in degrees Celsius above which the
processor clock is pulled back. Adding `gpiofanpin=<n>` switches a fan on
that pin instead and leaves the clock alone. Both are read by the SDL2 layer,
which owns the clock and the fan.

### Boot options

`rapi-split=0` in `cmdline.txt` puts everything back on core 0 — the game,
the drawing and the hardware — so the split and the single-core arrangement
can be compared on one image.

Every image also carries a 512-byte block at offset 0x800 that a boot loader
can write a command line into before the image runs. Anything written there
is added to the game's own arguments, so a boot can change the resolution or
load a saved game without rebuilding or rewriting the card. Arguments
starting `--rapi-` are the kernel's own and are removed before the game sees
them.

## How the layers fit

    host/                this repository's own code
      kernel.cpp         brings the board up, elects the cores, starts the game
      circle_syscalls.cpp  file access, routed to the core that owns the card
      circle_stubs.cpp   the SDL2 surface and palette layer OpenTTD needs
      circle_platform.cpp  the machine facilities OpenTTD expects an OS to have
      sdl2ext/           POSIX headers this board's C library does not carry
      defaults.cpp       the boot-time argument block described above
    openttd/             upstream OpenTTD, unmodified
    circle-libsdl2/      SDL2 on Circle, and the circle-stdlib worlds

`host/circle_stubs.cpp` is the largest piece of the port and the most likely
to shrink. OpenTTD's SDL2 backend uses the older window-surface model — ask
the window for a surface, draw into it, ask for it to be shown — and
circle-libsdl2 renders from textures. That file is the bridge between the
two, plus the palette handling the paletted blitters need. Each function in
it is a seam: when the SDL2 layer implements one for real, the way to adopt
it is to delete the copy here.

## License

The OpenTTD submodule is licensed under the GNU General Public License,
version 2, and this repository is under the same licence — see `LICENSE`. The
`circle-libsdl2` submodule and the Circle framework beneath it carry their
own licences, stated in their own repositories.

Nothing in this repository is affiliated with or endorsed by the OpenTTD
project.
