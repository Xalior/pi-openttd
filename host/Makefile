#
# OpenTTD as a bootable Raspberry Pi kernel image.
#
# One board per invocation (RAPI_BOARD, default rpi5). The board selects
# three things at once: the circle-stdlib world the kernel is compiled and
# linked against, the shim archive built against that world, and — through
# the world's Config.mk — the CPU tuning and RASPPI value. Each board's
# newlib and libc++ are built for its own architecture, so an image is
# never portable between boards.
#
#   make                     the default board's image
#   make RAPI_BOARD=rpi3     that board's image
#   make clean-board         drop just this board's build/<board>/ tree
#
# Everything this invocation generates lands under build/<board>/: the
# kernel's own objects, every OpenTTD object, and the final
# .elf/.map/.lst/.img. The object names are shared across boards but their
# contents are not (different -mcpu, different -DRASPPI, different world),
# so a flat build directory would silently relink one board's objects into
# another board's kernel — a corrupt image with no build error. A directory
# per board keeps all three isolated: no `make clean` between boards, and
# three boards can build at the same time without touching each other.
#
# OpenTTD's own source tree (../openttd) is never written into: it is
# read-only input, and every object it produces is redirected here.
#
# Prerequisites, both made by the repository root:
#   - the board's world and shim archive: `make deps`
#   - OpenTTD's generated sources:        `make generated`
# GEN_DIR names where the second of those landed.
#

include ../mk/toolchain.mk

BOARDS      = rpi3 rpi4 rpi5
RAPI_BOARD ?= rpi5

ROOT = ..
SHIM = $(ROOT)/circle-libsdl2
OTTD = $(ROOT)/openttd

# Where `make generated` at the repository root put OpenTTD's generated
# sources. Passed in by that makefile; the default is the same path, so
# `make -C host` on its own still finds them.
GEN_DIR ?= $(abspath $(ROOT)/build/gen)

OBJDIR = build/$(RAPI_BOARD)

# Circle's own image names, which the Raspberry Pi firmware selects between
# from the model sections of config.txt: the Pi 3 world boots kernel8.img,
# the Pi 4 world kernel8-rpi4.img, the Pi 5 world kernel_2712.img.
IMAGE_rpi3 = kernel8
IMAGE_rpi4 = kernel8-rpi4
IMAGE_rpi5 = kernel_2712
IMAGE = $(IMAGE_$(RAPI_BOARD))

# Before the world is included, so an unknown board is named as an unknown
# board. After the include it arrives as a missing Config.mk under a directory
# whose name nobody recognises.
ifeq ($(filter $(RAPI_BOARD),$(BOARDS)),)
$(error unknown RAPI_BOARD '$(RAPI_BOARD)' — expected one of: $(BOARDS))
endif

# The board's circle-stdlib world. A `make deps` that has not been run, or has
# not finished, leaves this file absent; make would then report that it has no
# rule to make a Config.mk, which describes the symptom and not the cause.
# WHERE THE BUILT WORLD COMES FROM.
#
# The default is this game's own nested shim, so a clone of this repository
# alone builds and uses its own worlds and nothing needs setting. A parent
# that keeps ONE set of built worlds for several games — they are gigabytes
# each, and identical when the pin and the configuration match — points
# CIRCLE_WORLDS at them.
CIRCLE_WORLDS ?= $(SHIM)
WORLD = $(CIRCLE_WORLDS)/circle-stdlib-$(RAPI_BOARD)
ifeq ($(wildcard $(WORLD)/Config.mk),)
$(error the $(RAPI_BOARD) circle-stdlib world is not configured ($(WORLD)/Config.mk is missing) — run 'make deps-$(RAPI_BOARD)' at the repository root)
endif

include $(WORLD)/Config.mk

TARGET = $(OBJDIR)/$(IMAGE)

# Stated explicitly because the first rule this file sees comes from an
# included makefile, and that would otherwise decide the default goal.
.DEFAULT_GOAL := image

# ---------------------------------------------------------------------------
# Sources — this port's own
# ---------------------------------------------------------------------------

HOST_SRCS = main.cpp kernel.cpp defaults.cpp circle_syscalls.cpp \
	circle_stubs.cpp circle_platform.cpp
HOST_OBJS = $(addprefix $(OBJDIR)/,$(HOST_SRCS:.cpp=.o))

# ---------------------------------------------------------------------------
# Sources — OpenTTD's
# ---------------------------------------------------------------------------
#
# ENUMERATED BY DIRECTORY, NOT BY FILE. OpenTTD is about five hundred
# translation units spread over thirty directories, and upstream's own build
# adds nearly all of them unconditionally: a hand-written list would be five
# hundred lines that went stale on the first pin bump, and a file added
# upstream would go silently unbuilt. So each directory that contributes is
# named here and swept, and the interesting half — what is deliberately NOT
# built, and why — is the exclusion list below. A file appearing upstream
# that this port cannot compile therefore breaks the build loudly, which is
# the right way round.
#
# The directory list is upstream's own, read off its CMakeLists files: every
# directory holding a source that its build compiles for a Unix SDL2 target.
OTTD_DIRS = . \
	3rdparty/fmt 3rdparty/md5 3rdparty/monocypher \
	3rdparty/squirrel/squirrel 3rdparty/squirrel/sqstdlib \
	ai blitter core fontcache game linkgraph misc music \
	network network/core newgrf os/unix pathfinder pathfinder/yapf \
	saveload script script/api sound spriteloader strgen timer video

# The files left out, each with the reason it cannot be here. Every one of
# them is a file upstream compiles only when a library or a machine feature
# is present, and none of those is present on this board.
#
#   gfx_layout_icu.cpp        ICU text layout: no ICU, no HarfBuzz
#   screenshot_png.cpp        libpng
#   soundloader_opus.cpp      libopusfile
#   soundresampler_soxr.cpp   libsoxr
#   viewport_sprite_sorter_sse4.cpp, blitter/*sse*.cpp
#                             x86 vector instructions; this is AArch64
#   blitter/40bpp_anim.cpp    the OpenGL blitter
#   fontcache/freetypefontcache.cpp
#                             FreeType
#   music/*                   every music backend but the null one and the
#                             external-player one needs a host sound system
#                             (ALSA sequencer, DirectMusic, CoreAudio,
#                             FluidSynth, BeOS MIDI)
#   sound/*                   the Allegro, CoreAudio, Windows and XAudio2
#                             sound backends; SDL2's is the one kept
#   video/allegro_v.cpp, win32_v.cpp
#                             other platforms' video backends
#   video/opengl.cpp, video/sdl2_opengl_v.cpp
#                             OpenGL: the shim has no GL context to give
#   network/core/http_curl.cpp, http_winhttp.cpp
#                             libcurl and WinHTTP; http_none.cpp, which
#                             upstream builds when neither is available, is
#                             kept instead and reports that HTTP is off
#   os/unix/font_unix.cpp     Fontconfig
#   os/unix/library_loader_unix.cpp
#                             dlopen: there is no dynamic loader on a bare
#                             metal board. The three LibraryLoader methods it
#                             would define are written in circle_platform.cpp
#                             instead, and report that a library cannot be
#                             opened
#   os/unix/unix_main.cpp     upstream's main(); built separately below with
#                             its entry point renamed, because main() here
#                             belongs to the Circle kernel
#   strgen/strgen.cpp         the string-table generator's own main(). The
#                             generator runs on the machine doing the
#                             building (see `make generated`); only the
#                             base it shares with the game is compiled here
OTTD_EXCLUDE = \
	gfx_layout_icu.cpp screenshot_png.cpp soundloader_opus.cpp \
	soundresampler_soxr.cpp viewport_sprite_sorter_sse4.cpp \
	blitter/32bpp_anim_sse2.cpp blitter/32bpp_anim_sse4.cpp \
	blitter/32bpp_sse2.cpp blitter/32bpp_sse4.cpp \
	blitter/32bpp_ssse3.cpp blitter/40bpp_anim.cpp \
	fontcache/freetypefontcache.cpp \
	music/allegro_m.cpp music/bemidi.cpp music/cocoa_m.cpp \
	music/dmusic.cpp music/fluidsynth.cpp music/win32_m.cpp \
	sound/allegro_s.cpp sound/cocoa_s.cpp sound/win32_s.cpp \
	sound/xaudio2_s.cpp \
	video/allegro_v.cpp video/opengl.cpp video/sdl2_opengl_v.cpp \
	video/win32_v.cpp \
	network/core/http_curl.cpp network/core/http_winhttp.cpp \
	os/unix/font_unix.cpp os/unix/library_loader_unix.cpp \
	os/unix/unix_main.cpp \
	strgen/strgen.cpp

# The sweep. `.`-prefixed paths are normalised away so the exclusion list can
# be written the way a person reads it.
OTTD_FOUND = $(patsubst ./%,%,$(patsubst $(OTTD)/src/%,%, \
	$(foreach d,$(OTTD_DIRS),$(wildcard $(OTTD)/src/$(d)/*.cpp $(OTTD)/src/$(d)/*.cc))))
OTTD_SRCS  = $(filter-out $(OTTD_EXCLUDE),$(OTTD_FOUND))

# Upstream's entry point, kept separate because it is compiled with its
# main() renamed (see the rule at the bottom).
OTTD_MAIN = os/unix/unix_main.cpp

OTTD_OBJS = $(addprefix $(OBJDIR)/ottd/,$(OTTD_SRCS:.cpp=.o)) \
	$(OBJDIR)/ottd/os/unix/unix_main.o
OTTD_OBJS := $(OTTD_OBJS:.cc=.o)

# The generated sources. rev.cpp is a real translation unit — the version
# the game reports — and is compiled like any other; the rest of the
# generation is headers, reached through the include path below.
GEN_OBJS = $(OBJDIR)/ottd/generated/rev.o

OBJS = $(HOST_OBJS) $(OTTD_OBJS) $(GEN_OBJS)

# C++23, not the C++20 OpenTTD's own build system asks for.
#
# The game itself is a C++20 program and would compile as one. The standard
# library it is compiled against will not: the libc++ this world was built
# with declares `static constexpr` variables inside constexpr functions in
# its own headers — <algorithm>, and everything that includes it — and that
# is a C++23 construct. GCC 15 diagnoses a template body as it parses it, so
# the first translation unit to include <string> fails outright at C++20.
#
# C++23 is a superset for everything this build does, and it is the standard
# the library on the other side of the include is written in, so it is the
# one the whole build uses.
STANDARD = -std=c++23

# Stated, not inherited: this is Circle's own default, written down so the
# build's optimization level is a fact in the file rather than a forensic
# reconstruction.
OPTIMIZE = -O2

# Dependency files are written as a side effect of compiling rather than by
# rules of their own, and Circle's Rules.mk is told to keep its hands off
# (CHECK_DEPS) so its `-M -MG` .d rules are not defined for these objects.
#
# -MG lists a header make has no rule for as a prerequisite of the object,
# which stops the build; GNU make 3.81 stops with exit 2 and prints not one
# line saying why, which is why this build asks for 4.0 in mk/toolchain.mk. A
# .d left over from an earlier build naming a header that has since moved or
# been deleted stops it the same way, and is why "delete the board's object
# directory" kept being the cure.
#
# -MD writes the dependency file alongside the object it belongs to, so the
# two can never disagree about the flags that produced them, and -MP adds a
# rule with no recipe for every header named, so a header that disappears
# means a rebuild instead of a dead stop.
DEPS       = $(OBJS:.o=.d)
CHECK_DEPS = 0
DEPFLAGS   = -MD -MP

include $(CIRCLEHOME)/Rules.mk

# The world must be the multicore one circle-libsdl2 configures: the shim's
# core-split machinery cannot exist without it, and an object compiled
# against a single-core world links without complaint and then misbehaves.
# Every object here inherits the flag through Rules.mk's CPPFLAGS, so this
# check covers the whole build.
ifeq ($(findstring ARM_ALLOW_MULTI_CORE,$(DEFINE)),)
$(error the payload requires the multicore circle-stdlib world — build it with 'make -C ../circle-libsdl2 deps')
endif

# WHERE THIS GAME LIVES ON THE CARD. One card carries several games, and two
# of them writing a `settings.ini` or a `logo.png` into the FAT root would
# each silently overwrite the other's. Every game therefore keeps to a
# directory of its own, and nothing of this port's ever touches the root.
#
# It is a build-time constant, not a boot switch: a card's layout is decided
# when the card is staged, and a game that could be relocated at boot would
# only mean two places to look for the same file.
#
# The path is used ONLY by this port's own host layer — argv, the working
# directory, and SDL's base and preference paths. Upstream OpenTTD is never
# told about it and needs no patch: it looks where it is pointed.
RAPI_GAME_DIR ?= /games/openttd
DEFINE += -DRAPI_GAME_DIR='"$(RAPI_GAME_DIR)"'

# ---------------------------------------------------------------------------
# What OpenTTD is told about the machine it is being built for
# ---------------------------------------------------------------------------
#
# These are upstream's own switches, set to the same values its CMake build
# would set for a 64-bit Unix machine with SDL2 and nothing else:
#
#   UNIX             selects the POSIX file, path and socket code
#   POINTER_IS_64BIT the pointer width, which upstream asks for rather than
#                    deducing
#   WITH_SDL2        compiles the SDL2 video and sound backends and makes
#                    them the drivers the game can pick
#   NDEBUG           a release build; upstream's asserts are separately
#                    controlled by WITH_ASSERT, which is not set
#
# Deliberately NOT set, each because the library behind it does not exist
# here: WITH_ZLIB, WITH_LIBLZMA, WITH_LZO (savegame compression — the
# uncompressed format upstream always carries is what remains), WITH_PNG
# (screenshots become BMP and PCX), WITH_CURL (no HTTP client, so no content
# downloads), WITH_ICONV, WITH_FREETYPE, WITH_FONTCONFIG, WITH_ICU_I18N and
# WITH_HARFBUZZ (the built-in sprite font is what draws text), WITH_OPENGL.
#
# Also not set: WITH_PERSONAL_DIR and WITH_SHARED_DIR. On a desktop they put
# the configuration under the user's home directory and the data under a
# system one. There is no user and no system here — there is one directory
# on a card, and RAPI_GAME_DIR above is it.
OTTD_DEFINE = -DUNIX -DPOINTER_IS_64BIT -DWITH_SDL2 -DNDEBUG

# -fno-strict-enums is upstream's, and it is not cosmetic: OpenTTD stores
# values outside an enum's declared range in enum-typed variables, and a
# compiler assuming otherwise deletes the code that checks for them.
#
# -include cstdlib is this port's, and it is here because of the standard
# library rather than because of OpenTTD. The bundled {fmt} calls malloc and
# free unqualified in its own allocator and never includes <cstdlib> for
# them; on a desktop some other standard header has always already declared
# them by the time that code is reached, and with the libc++ these worlds are
# built from, none has. Forcing the header in front of every translation unit
# is the smallest fix that leaves the vendored tree untouched, and it costs
# nothing anywhere else.
OTTD_CXXFLAGS = -fno-strict-enums -finput-charset=utf-8 -include cstdlib \
	$(OTTD_DEFINE) $(CPPFLAGS)

# The include path, in upstream's own order:
#
#   sdl2ext/            this port's own headers that stand in for POSIX ones
#                       newlib does not carry. They are OURS, in this
#                       directory, and upstream is not modified to find them
#                       — it just includes <ifaddrs.h> as it always has.
#   the shim            SDL2/-prefixed and bare, because OpenTTD writes
#                       `#include <SDL.h>` the way a desktop sdl2-config
#                       include path provides it
#   generated/          the strings table, the settings table and rev.h's
#                       companion, made by `make generated`
#   squirrel/include    the script engine's public headers, which upstream
#                       puts on the path for the whole build
#   the script API      three parallel pairs of directories, generated and
#                       source, one pair per script API
INCLUDE := -I $(CURDIR)/sdl2ext \
	-I $(SHIM)/include -I $(SHIM)/include/SDL2 \
	-I $(OTTD)/src/3rdparty/squirrel/include \
	-I $(GEN_DIR)/generated \
	-I $(GEN_DIR)/generated/script -I $(GEN_DIR)/generated/script/api \
	-I $(GEN_DIR)/generated/script/api/ai \
	-I $(GEN_DIR)/generated/script/api/game \
	-I $(GEN_DIR)/generated/script/api/template \
	-I $(OTTD)/src/script/api \
	-I $(OTTD)/src/script/api/ai \
	-I $(OTTD)/src/script/api/game \
	-I $(OTTD)/src/script/api/template \
	$(CIRCLE_STDLIB_INCLUDES) $(INCLUDE)

# The shim comes in whole, every member, rather than only the members the
# link happens to need. An object file linked straight into the kernel beats
# an archive member of the same name and does it silently, so a stub in
# circle_stubs.cpp left behind after the library implemented the real thing
# would quietly win — the game would keep calling the stub and nothing would
# say so. Pulling the archive in whole turns that into a duplicate-symbol
# error at link time, which is the only moment it can still be cheap to fix.
SHIM_LIB = $(SHIM)/libSDL2-$(RAPI_BOARD).a

LIBS = --whole-archive $(SHIM_LIB) --no-whole-archive \
	$(CIRCLE_STDLIB_LIBS)

# The shim archive is built in circle-libsdl2 and never here — it is a shared
# dependency, and a rule here would rebuild it at whatever board and crossing
# count this makefile happens to be running with. This rule exists only for the
# case where it is absent: it runs then, and only then, because a target with a
# recipe and no prerequisites is up to date as soon as the file exists.
$(SHIM_LIB):
	@echo "$@ is missing: the $(RAPI_BOARD) shim archive has not been built."
	@echo "Run: $(MAKE) -C $(SHIM) libSDL2-$(RAPI_BOARD).a BOARD=$(RAPI_BOARD)"
	@exit 1

# ---------------------------------------------------------------------------
# Filesystem syscalls, redirected at link time
# ---------------------------------------------------------------------------
#
# The game runs on the application core, and newlib's Circle glue drives
# FatFs and the SD card straight from wherever it is called — which off core
# 0 is illegal. circle_syscalls.cpp puts the shim's any-core I/O service
# underneath the C library instead; these flags are what put it there.
#
# --wrap rather than redefinition, because redefinition cannot work here:
# the glue defines _open, _read, _write, _close, _lseek, _fstat, opendir,
# readdir, closedir, dup and more in a SINGLE object file. Redefining part
# of that set either collides at link time or, worse, leaves the originals
# linked in beside the replacements with no diagnostic. --wrap renames the
# references instead and leaves the vendored glue untouched — and it is what
# keeps __real_* reachable, which is how the service performs the real work
# once it has reached core 0.
#
# Every name here must also appear in circle_syscalls.cpp: a wrap with no
# __wrap_ definition is an undefined reference, so the link is the check.
WRAPPED_SYSCALLS = \
	_open _close _read _write _lseek _fstat _stat lstat _unlink _rename \
	_fcntl mkdir chdir getcwd dup dup2 \
	opendir readdir closedir rewinddir

# circle-libsdl2 now implements SDL_CreateRGBSurface and SDL_FreeSurface for
# paletted surfaces as well as 32-bit ones, so this port no longer needs to
# intercept the pair the way it wraps the syscalls above — there is nothing
# left for circle_stubs.cpp to add on top, and no --wrap for them here.
#
# The link is driven by the cross ld directly (Circle's Rules.mk sets
# LD = <prefix>ld for GCC builds), so these are plain linker options, not
# compiler-driver -Wl, ones.
LDFLAGS += $(addprefix --wrap=,$(WRAPPED_SYSCALLS))

# ---------------------------------------------------------------------------
# Compilation
# ---------------------------------------------------------------------------

# This project's own objects, board-scoped. These more-specific rules win
# over Rules.mk's in-place %.o/%.d, so nothing is written beside the source.
$(OBJDIR)/%.o: %.cpp | $(OBJDIR)
	@echo "  CPP   $@"
	@$(CPP) $(CPPFLAGS) $(OTTD_DEFINE) $(DEPFLAGS) -c -o $@ $<

# OpenTTD's objects, mirroring its source tree under $(OBJDIR)/ottd/. The
# source tree itself is the upstream submodule, and is never written to.
$(OBJDIR)/ottd/%.o: $(OTTD)/src/%.cpp
	@mkdir -p $(dir $@)
	@echo "  CPP   $@"
	@$(CPP) $(OTTD_CXXFLAGS) $(DEPFLAGS) -c -o $@ $<

# The bundled {fmt} carries two .cc files, which upstream compiles as C++
# like every other source here.
$(OBJDIR)/ottd/%.o: $(OTTD)/src/%.cc
	@mkdir -p $(dir $@)
	@echo "  CPP   $@"
	@$(CPP) $(OTTD_CXXFLAGS) $(DEPFLAGS) -c -o $@ $<

# The generated sources, which live in the build directory rather than the
# source tree.
$(OBJDIR)/ottd/generated/%.o: $(GEN_DIR)/generated/%.cpp
	@mkdir -p $(dir $@)
	@echo "  CPP   $@"
	@$(CPP) $(OTTD_CXXFLAGS) $(DEPFLAGS) -c -o $@ $<

# OpenTTD's main() is the program's entry point on a desktop, but here
# main() belongs to the Circle kernel (main.cpp in this directory), which
# calls the game once the board is up. Renaming the entry point through the
# preprocessor for this one translation unit keeps upstream's source
# untouched, and the kernel calls the new name.
$(OBJDIR)/ottd/os/unix/unix_main.o: $(OTTD)/src/os/unix/unix_main.cpp
	@mkdir -p $(dir $@)
	@echo "  CPP   $@ (entry point renamed)"
	@$(CPP) $(OTTD_CXXFLAGS) $(DEPFLAGS) -Dmain=openttd_unix_main -c -o $@ $<

$(OBJDIR):
	@mkdir -p $(OBJDIR)

-include $(DEPS)

# The image link rule. sdl-app.mk lets the caller choose the linker script,
# so this build supplies its own: the shim's script with room made at image
# offset 0x800 for the patchable-defaults block. Everything else about the
# script — including the TLS-section adjacency binutils 2.44 and later
# require — is inherited from the shim's.
SDL_APP_LDSCRIPT = $(CURDIR)/openttd-defaults.ld
include $(SHIM)/sdl-app.mk

# The other half of the guarantee the linker script's ASSERTs begin: check
# the BYTES of the finished image, not just the link. A pre-boot writer
# refuses to stamp an image whose magic is not at 0x800, so an image that
# lost it would boot and then quietly ignore everything ever sent to it —
# including --rapi-debug-uart, which is the channel used to drive the board
# when nobody is at the bench. Better to have no image than that one.
.PHONY: image
image: $(TARGET).img
	@if [ "`dd if=$(TARGET).img bs=4 skip=512 count=1 2>/dev/null`" = "PM8D" ]; \
	then \
		echo "  GATE  $(TARGET).img defaults block at 0x800: present"; \
	else \
		echo "  GATE  $(TARGET).img: defaults block ABSENT at 0x800 — image deleted"; \
		rm -f $(TARGET).img; \
		exit 1; \
	fi

# Board-scoped clean: drop just this board's tree, leaving the other two
# boards' images and objects untouched.
.PHONY: clean-board
clean-board:
	rm -rf $(OBJDIR)

# This board from nothing. An incremental build is a decision made from
# timestamps, and a timestamp says when a file was written rather than what is
# in it; anything read off the resulting image describes what the last build
# happened to put there. Take a measurement that has to be right off one of
# these.
.PHONY: rebuild
rebuild:
	+@$(NOT_DRY_RUN)
	@$(MAKE) clean-board RAPI_BOARD=$(RAPI_BOARD)
	@$(MAKE) image RAPI_BOARD=$(RAPI_BOARD)
