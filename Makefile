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
#   make media               download the freely redistributable base sets
#                            into media/
#   make netboot             stage the Pi 5 image and its boot configuration
#                            into build/netboot-rpi5/
#   make card                stage the whole card into build/sd-card/, copying
#                            in whatever media/ holds and naming what it does
#                            not. It never downloads anything
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

.PHONY: deps generated kernels verify media netboot card clean-boards $(BOARDS)
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

# ---------------------------------------------------------------------------
# Game data
# ---------------------------------------------------------------------------
#
#   media/           what `make media` downloads. Gitignored, never shipped,
#                    and never part of a build.
#   build/sd-card/   what `make card` stages. It copies from media/ and
#                    fetches nothing.
#
# `card` does not depend on `media`, so a card built without it is complete
# except for the data and names the files that are absent.
#
# `make media` fetches the three free base sets published by the OpenTTD
# project at cdn.openttd.org: OpenGFX (graphics, required), OpenSFX (sound
# effects) and OpenMSX (music). The original Transport Tycoon Deluxe data
# files are a commercial product and are never fetched — a copy is supplied
# by hand, as the README describes.
#
# Each set is published as a .zip holding exactly one file, a .tar OpenTTD
# reads directly without unpacking further. The zip is downloaded, checked
# against the SHA256 published in the release's own manifest.yaml on the
# same host, and unpacked; the .tar that lands in media/ is then checked
# against the SHA256 this project computed from that extraction, since
# nothing publishes a checksum for the unpacked file on its own. Re-running
# re-verifies the .tar already in media/ rather than re-downloading.
MEDIA_DIR = media

OPENGFX_URL        = https://cdn.openttd.org/opengfx-releases/8.0/opengfx-8.0-all.zip
OPENGFX_ZIP_SHA256 = 43a0c1dabf39cb865394f3a6cc36d4da5c10ecfaaf55652043104806810903be
OPENGFX_TAR_SHA256 = 9389bcb0807058c80bd95121e978f05d9ef86b4b1bc3ac2da8da8bb02456043c

OPENSFX_URL        = https://cdn.openttd.org/opensfx-releases/1.0.3/opensfx-1.0.3-all.zip
OPENSFX_ZIP_SHA256 = e0a218b7dd9438e701503b0f84c25a97c1c11b7c2f025323fb19d6db16ef3759
OPENSFX_TAR_SHA256 = 531a243c5f0742e34d53704263302bbb847a3dc1e618831097ee940088b7b879

OPENMSX_URL        = https://cdn.openttd.org/openmsx-releases/0.4.2/openmsx-0.4.2-all.zip
OPENMSX_ZIP_SHA256 = 5a4277a2e62d87f2952ea5020dc20fb2f6ffafdccf9913fbf35ad45ee30ec762
OPENMSX_TAR_SHA256 = d8c8062d1c1cc7df2a89e7ce3bdaceab58d966a86142b09c788aa38940223191

# sha256sum on Linux, shasum on macOS. Whichever exists; if neither does, the
# target stops rather than accepting a download it cannot check.
SHA256SUM := $(firstword $(shell command -v sha256sum 2>/dev/null) \
                         $(shell command -v shasum 2>/dev/null))

media:
	@if [ -z "$(SHA256SUM)" ]; then \
		echo "  MEDIA no sha256sum or shasum on this machine — refusing to"; \
		echo "        download something that cannot be verified."; \
		exit 1; \
	fi
	@command -v unzip >/dev/null 2>&1 || { \
		echo "  MEDIA no unzip on this machine — refusing to unpack a"; \
		echo "        download that cannot then be verified."; \
		exit 1; \
	}
	@mkdir -p $(MEDIA_DIR)
	@fetch_baseset() { \
		name="$$1"; url="$$2"; zipsha="$$3"; tarsha="$$4"; \
		tar_path="$(MEDIA_DIR)/$$name.tar"; \
		zip_path="$(MEDIA_DIR)/$$name-all.zip"; \
		if [ -f "$$tar_path" ]; then \
			echo "  MEDIA $$tar_path already here — verifying"; \
		else \
			echo "  MEDIA fetching $$url"; \
			curl -fL --retry 3 -o "$$zip_path.part" "$$url" || { \
				rm -f "$$zip_path.part"; \
				echo "  MEDIA download failed for $$url"; return 1; }; \
			mv "$$zip_path.part" "$$zip_path"; \
			got=`$(SHA256SUM) -a 256 "$$zip_path" 2>/dev/null || $(SHA256SUM) "$$zip_path"`; \
			got=`echo "$$got" | awk '{print $$1}'`; \
			if [ "$$got" != "$$zipsha" ]; then \
				echo "  MEDIA SHA256 MISMATCH for $$zip_path"; \
				echo "        expected $$zipsha"; \
				echo "        got      $$got"; \
				echo "        the file has been left in place for inspection, and is"; \
				echo "        NOT safe to unpack."; \
				return 1; \
			fi; \
			unzip -p "$$zip_path" "$$name.tar" > "$$tar_path.part" || { \
				rm -f "$$tar_path.part"; \
				echo "  MEDIA could not extract $$name.tar from $$zip_path"; \
				return 1; }; \
			mv "$$tar_path.part" "$$tar_path"; \
			rm -f "$$zip_path"; \
		fi; \
		got=`$(SHA256SUM) -a 256 "$$tar_path" 2>/dev/null || $(SHA256SUM) "$$tar_path"`; \
		got=`echo "$$got" | awk '{print $$1}'`; \
		if [ "$$got" != "$$tarsha" ]; then \
			echo "  MEDIA SHA256 MISMATCH for $$tar_path"; \
			echo "        expected $$tarsha"; \
			echo "        got      $$got"; \
			echo "        the file has been left in place for inspection, and is"; \
			echo "        NOT safe to put on a card."; \
			return 1; \
		fi; \
		tar -tf "$$tar_path" >/dev/null 2>&1 || { \
			echo "  MEDIA $$tar_path does not read back as a tar archive"; \
			return 1; }; \
		echo "  MEDIA $$tar_path verified ($$(wc -c < "$$tar_path" | tr -d ' ') bytes)"; \
	}; \
	fail=0; \
	fetch_baseset opengfx-8.0   "$(OPENGFX_URL)" "$(OPENGFX_ZIP_SHA256)" "$(OPENGFX_TAR_SHA256)" || fail=1; \
	fetch_baseset opensfx-1.0.3 "$(OPENSFX_URL)" "$(OPENSFX_ZIP_SHA256)" "$(OPENSFX_TAR_SHA256)" || fail=1; \
	fetch_baseset openmsx-0.4.2 "$(OPENMSX_URL)" "$(OPENMSX_ZIP_SHA256)" "$(OPENMSX_TAR_SHA256)" || fail=1; \
	exit $$fail
	@printf '%s\n' \
		"OpenTTD base sets — free replacements for the original Transport" \
		"Tycoon Deluxe graphics, sounds and music, published by the OpenTTD" \
		"project so the game runs without the paid original data." \
		"" \
		"Fetched with plain curl, verified against the SHA256 published in" \
		"each release's own manifest.yaml on the same host (cdn.openttd.org)." \
		"" \
		"opengfx-8.0.tar (graphics, required to start at all)" \
		"  Source:  $(OPENGFX_URL)" \
		"  Licence: GNU GPL v2 (license.txt inside the tar)" \
		"  Zip SHA256 (published, matches manifest.yaml): $(OPENGFX_ZIP_SHA256)" \
		"  Tar SHA256 (computed from this project's own download): $(OPENGFX_TAR_SHA256)" \
		"" \
		"opensfx-1.0.3.tar (sound effects)" \
		"  Source:  $(OPENSFX_URL)" \
		"  Licence: Creative Commons Attribution-ShareAlike 3.0 Unported (license.txt inside the tar)" \
		"  Zip SHA256 (published, matches manifest.yaml): $(OPENSFX_ZIP_SHA256)" \
		"  Tar SHA256 (computed from this project's own download): $(OPENSFX_TAR_SHA256)" \
		"" \
		"openmsx-0.4.2.tar (music)" \
		"  Source:  $(OPENMSX_URL)" \
		"  Licence: GNU GPL v2 (license.txt inside the tar)" \
		"  Zip SHA256 (published, matches manifest.yaml): $(OPENMSX_ZIP_SHA256)" \
		"  Tar SHA256 (computed from this project's own download): $(OPENMSX_TAR_SHA256)" \
		"" \
		"Fetched: `date -u '+%Y-%m-%d %H:%M:%S UTC'`" \
		"" \
		"Destination on the card: games/openttd/baseset/ — flat, no" \
		"subdirectories. OpenTTD reads a .tar directly as a base set, so" \
		"each file is staged exactly as unpacked from its zip." \
		"" \
		"The original Transport Tycoon Deluxe data files are not fetched" \
		"here. They are a commercial product and are not distributed by" \
		"anyone; a copy is supplied by hand if you own one — see README.md." \
		> $(MEDIA_DIR)/provenance.txt
	@echo "  MEDIA provenance written to $(MEDIA_DIR)/provenance.txt"

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
# This target downloads nothing. It copies what `make media` left in
# media/ into $(GAME_DIR)/baseset/, alongside the base set metadata and GUI
# sprites OpenTTD's own build already generates, and names what is absent.
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
	@for f in opengfx-8.0.tar opensfx-1.0.3.tar openmsx-0.4.2.tar; do \
		if [ -f "$(MEDIA_DIR)/$$f" ]; then \
			cp "$(MEDIA_DIR)/$$f" $(GAME_DIR)/baseset/; \
			echo "  DATA   $$f"; \
		fi; \
	done
	@echo
	@if [ -f $(GAME_DIR)/baseset/opengfx-8.0.tar ]; then :; else \
		echo "  ABSENT opengfx-8.0.tar — the graphics base set. OpenTTD"; \
		echo "         refuses to start without one; 'make media' fetches"; \
		echo "         the free OpenGFX set."; \
	fi
	@if [ -f $(GAME_DIR)/baseset/opensfx-1.0.3.tar ]; then :; else \
		echo "  ABSENT opensfx-1.0.3.tar — the sound base set. The game"; \
		echo "         starts without it but plays no sound effects;"; \
		echo "         'make media' fetches the free OpenSFX set."; \
	fi
	@if [ -f $(GAME_DIR)/baseset/openmsx-0.4.2.tar ]; then :; else \
		echo "  ABSENT openmsx-0.4.2.tar — the music base set. The game"; \
		echo "         starts without it but plays no music; 'make media'"; \
		echo "         fetches the free OpenMSX set."; \
	fi
	@echo "  NOTE   The Raspberry Pi firmware files are not staged here either."
	@echo "         See README.md."

clean-boards:
	@for b in $(BOARDS); do $(MAKE) -C host RAPI_BOARD=$$b clean-board; done
	rm -rf $(NETBOOT_DIR) $(CARD_DIR)
