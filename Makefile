#
# pi-openttd — OpenTTD as a bootable bare-metal Raspberry Pi image.
#
#   make check-toolchain     report the cross compiler this build will use
#   make deps                the three circle-stdlib worlds and the shim
#                            archives built against them (long: the worlds
#                            build newlib and libc++ from source)
#   make deps-rpi4           the same for one board only, for a machine that
#                            cannot hold three worlds at once
#   make generated           OpenTTD's generated sources — the strings table,
#                            the settings table, the script bindings, the
#                            revision file, the compiled language files and
#                            the base set metadata
#   make rpi5 | rpi4 | rpi3  one board's kernel image
#   make kernels             all three, built in parallel
#   make verify              truth-gate: every image exists and is non-empty
#   make netboot             stage the Pi 5 image and its boot configuration
#                            into build/netboot-rpi5/
#   make card                stage the whole card, except the base set data,
#                            into build/sd-card/
#   make clean-boards        drop every board's build tree
#
# The three boards never share mutable state: each has its own circle-stdlib
# world, its own shim archive and its own object directory, so building them
# at the same time is safe and building one never disturbs another.
#
# The libc++ sources every world is built from are one immutable git tag, and
# CIRCLE_LLVM says where that checkout lives. The default puts it beside this
# repository, which is right for a plain clone and for a CI runner. Point
# several projects at one directory to fetch it once for all of them:
#
#   make deps CIRCLE_LLVM=/path/to/circle-llvm
#
# OPENTTD'S OWN BUILD SYSTEM IS STILL USED, FOR ONE JOB. Several of its
# sources do not exist until they are generated: the string identifiers, the
# settings table, the Squirrel script bindings and the revision file. The
# generators are C++ programs that have to run on the machine doing the
# building, and the rest of the generation is CMake script. Rather than
# reimplement any of it, `make generated` runs upstream's CMake once with the
# host compiler and takes the results. So CMake is a build requirement here,
# for the host side only; the cross build itself is plain make.
#

include mk/toolchain.mk

# Stated explicitly because the first rule this file sees comes from an
# included makefile, and that would otherwise decide the default goal.
.DEFAULT_GOAL := kernels

BOARDS ?= rpi3 rpi4 rpi5

IMAGE_rpi3 = kernel8.img
IMAGE_rpi4 = kernel8-rpi4.img
IMAGE_rpi5 = kernel_2712.img

# Where the host-side generation lands. Board-independent — the generated
# sources say nothing about the machine they will be compiled for — so all
# three boards read the one copy.
GEN_DIR    = $(CURDIR)/build/gen
GEN_STAMP  = $(GEN_DIR)/generated/table/strings.h

.PHONY: deps generated kernels verify netboot card clean-boards $(BOARDS)
.PHONY: $(addprefix deps-,$(BOARDS))

deps:
	$(MAKE) -C circle-libsdl2 deps

# One board's dependencies: its own circle-stdlib world and the shim archive
# built against it. A machine with a small disk — a CI runner, most obviously
# — builds one board at a time and keeps only that board's world.
# Written as a static pattern rule over the board list rather than a plain
# pattern rule: these targets are phony, and make does not apply pattern rules
# to phony targets — it would quietly answer "nothing to be done" and leave
# the world unbuilt.
$(addprefix deps-,$(BOARDS)): deps-%:
	$(MAKE) -C circle-libsdl2 world BOARD=$*
	$(MAKE) -C circle-libsdl2 libSDL2-$*.a BOARD=$*

# ---------------------------------------------------------------------------
# OpenTTD's generated sources
# ---------------------------------------------------------------------------
#
# Run once, with the machine's own compiler, and shared by all three boards.
# The targets asked for are exactly the ones that produce files:
#
#   find_version    generated/rev.cpp, the revision the game reports
#   table_strings   generated/table/strings.h, the STR_* identifiers
#   table_settings  generated/table/settings.h, the settings table
#   script_window   generated/script/api/script_window.hpp
#   script_ai/game/template
#                   the Squirrel binding headers for the three script APIs
#   language_files  lang/*.lng, the compiled language files the game loads
#   baseset_files   baseset/*, the metadata and the GUI sprites the game
#                   needs before any downloaded graphics set
#   ai_compat_files, gs_compat_files
#                   the script compatibility layers, loaded from the card
#
# Nothing of the game itself is built here, and the configuration is only
# ever asked about the host: the values it finds for zlib, libpng and the
# rest never reach the cross build.
generated: $(GEN_STAMP)

$(GEN_STAMP):
	@echo "  CMAKE openttd host-side generation -> $(GEN_DIR)"
	@cmake -S openttd -B $(GEN_DIR) -DCMAKE_BUILD_TYPE=Release -DOPTION_DEDICATED=ON >/dev/null
	@cmake --build $(GEN_DIR) --target \
		find_version table_strings table_settings \
		script_window script_ai script_game script_template \
		language_files baseset_files ai_compat_files gs_compat_files >/dev/null
	@test -s $(GEN_STAMP) || { echo "  FAIL  generation produced no strings table"; exit 1; }
	@echo "  GEN   $(GEN_DIR)/generated, $(GEN_DIR)/lang, $(GEN_DIR)/baseset"

$(BOARDS): check-toolchain generated
	$(MAKE) -C host RAPI_BOARD=$@ GEN_DIR=$(GEN_DIR)

# All three at once. Each sub-make owns a different world and a different
# output directory, so there is nothing for them to collide on. The generated
# sources are made first, in this rule's own prerequisites, because three
# parallel sub-makes racing to run one CMake would not be.
#
# Each board is waited for BY PID, and its status kept. A bare `wait` reports
# only that the shell has no children left — it is success whatever the jobs
# did — so a board that failed to build would leave this target reporting
# success, and the truth-gate would then pass the board's PREVIOUS image,
# still on disk.
kernels: check-toolchain generated
	@pids=; fail=0; \
	for b in $(BOARDS); do $(MAKE) -C host RAPI_BOARD=$$b GEN_DIR=$(GEN_DIR) & pids="$$pids $$!"; done; \
	for p in $$pids; do wait $$p || fail=1; done; \
	exit $$fail

# Truth-gate: ask the filesystem, not the exit codes. An image that is
# missing or empty fails here even if the build claimed success.
verify:
	@fail=0; \
	for b in $(BOARDS); do \
		case $$b in \
			rpi3) img=host/build/rpi3/$(IMAGE_rpi3) ;; \
			rpi4) img=host/build/rpi4/$(IMAGE_rpi4) ;; \
			rpi5) img=host/build/rpi5/$(IMAGE_rpi5) ;; \
		esac; \
		if [ -s "$$img" ]; then \
			echo "  OK    $$img ($$(wc -c < $$img | tr -d ' ') bytes)"; \
		else \
			echo "  FAIL  $$img missing or empty"; fail=1; \
		fi; \
	done; \
	exit $$fail

# The Pi 5 netboot bundle: the image the Pi 5 firmware looks for, plus the
# boot configuration it must be served alongside. Copy the contents into the
# TFTP root the board boots from (the Raspberry Pi firmware files themselves
# come from that root's existing installation, not from here).
NETBOOT_DIR = build/netboot-rpi5
netboot: rpi5
	@mkdir -p $(NETBOOT_DIR)
	@cp host/build/rpi5/$(IMAGE_rpi5) $(NETBOOT_DIR)/
	@cp host/config.txt host/cmdline.txt $(NETBOOT_DIR)/
	@echo "  STAGED $(NETBOOT_DIR)/"
	@ls -l $(NETBOOT_DIR)/

# The card, staged into a directory to copy onto media formatted elsewhere:
# the three kernels and the boot configuration at the root, and everything
# the game reads under the one directory it is given.
#
# The graphics, sounds and music are NOT here and are not ours to ship — see
# the README for what to put in baseset/ and where it legitimately comes
# from.
CARD_DIR  = build/sd-card
GAME_DIR  = $(CARD_DIR)/games/openttd
card: kernels
	@rm -rf $(CARD_DIR)
	@mkdir -p $(GAME_DIR)
	@cp host/build/rpi3/$(IMAGE_rpi3) $(CARD_DIR)/
	@cp host/build/rpi4/$(IMAGE_rpi4) $(CARD_DIR)/
	@cp host/build/rpi5/$(IMAGE_rpi5) $(CARD_DIR)/
	@cp host/config.txt host/cmdline.txt $(CARD_DIR)/
	@cp -R $(GEN_DIR)/baseset $(GAME_DIR)/
	@cp -R $(GEN_DIR)/lang $(GAME_DIR)/
	@cp -R $(GEN_DIR)/ai $(GAME_DIR)/
	@cp -R $(GEN_DIR)/game $(GAME_DIR)/
	@cp host/openttd.cfg $(GAME_DIR)/
	@echo "  STAGED $(CARD_DIR)/"
	@echo "  The Raspberry Pi firmware files and the graphics, sound and"
	@echo "  music sets are not staged here. Put a graphics set in"
	@echo "  $(GAME_DIR)/baseset/ before writing the card."

clean-boards:
	@for b in $(BOARDS); do $(MAKE) -C host RAPI_BOARD=$$b clean-board; done
	rm -rf $(NETBOOT_DIR) $(CARD_DIR)
