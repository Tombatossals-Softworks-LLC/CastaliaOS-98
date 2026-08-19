# =====================================================================
#  CastaliaOS 98 PE - Host build orchestration
#
#  This Makefile builds the PORTABLE upper stack + the headless HOST
#  platform backend with a normal C compiler (gcc/clang). It produces:
#    build/castalia    - the shell driven by the headless backend
#    build/run_tests   - the unit-test runner
#
#  The real DOS product is built separately with Open Watcom via
#  `wmake -f Makefile.dos` (see docs/BUILDING.md). The DOS backend under
#  src/platform/dos is intentionally NOT part of this build, so DOS-only
#  code can never break host portability.
#
#  Targets:
#    make            - build the host shell
#    make run        - build + render a demo desktop to build/castalia.bmp
#    make test       - build + run unit tests
#    make clean
# =====================================================================

CC      ?= cc
# GNU C89: strict C89 *language* (Watcom-compatible) but with the standard
# library declarations (snprintf, opendir, ...) our platform layer needs.
CSTD    ?= -std=gnu89
#
# Beyond -Wall -Wextra, six that pay for themselves in C89 and cost nothing
# here because the tree is already clean under them:
#
#   -Wstrict-prototypes   an empty () is not "no arguments" in C89, it is
#                         "unchecked", and every call to it goes unverified
#   -Wmissing-prototypes  a non-static function nobody declared is either
#                         dead or wanted to be static; sh_switch_draw was
#   -Wredundant-decls     the same prototype in two headers is the same
#                         decision in two places, and they drift
#   -Wnested-externs      a declaration hidden inside a function body is one
#                         no header can be checked against
#   -Wold-style-definition  K&R definitions skip argument checking entirely
#   -Wpointer-arith       arithmetic on void*/function pointers is a GNU
#                         extension Watcom will not take
#
# -Wshadow is deliberately NOT here: the only hits are inner blocks declaring
# their own loop index, which is ordinary C89 style rather than a defect.
WARN    ?= -Wall -Wextra -Wno-unused-parameter \
           -Wstrict-prototypes -Wmissing-prototypes -Wredundant-decls \
           -Wnested-externs -Wold-style-definition -Wpointer-arith
OPT     ?= -O2 -g
DEFS    ?= -DCASTALIA_HOST
INCS    := -Iinclude -Isrc/apps -Itests
CFLAGS  ?= $(CSTD) $(WARN) $(OPT) $(DEFS) $(INCS)

BUILD   ?= build

# ---- portable sources shared by the shell and the tests --------------
CORE_SRC := \
  src/sys/sys_log.c src/sys/sys_str.c src/sys/sys_mem.c \
  src/sys/sys_time.c src/sys/sys_crash.c src/sys/snd_common.c src/sys/cpu_core.c src/sys/mach_core.c src/sys/cal_core.c src/sys/sys_le.c src/sys/sys_home.c \
  src/sys/clip.c \
  src/cfg/cfg_ini.c src/cfg/settings.c src/cfg/lastgood.c \
  src/capp/capp.c src/capp/capp_scan.c \
  src/capp/capp_loader.c src/capp/capp_loader_io.c \
  src/capp/capp_host_wm.c src/capp/capp_sample.c \
  src/net/net_util.c src/net/net_stack.c \
  src/gfx/gfx_rect.c src/gfx/gfx_surface.c src/gfx/gfx_draw.c \
  src/gfx/gfx_blit.c src/gfx/gfx_font.c src/gfx/gfx_font_data.c \
  src/gfx/gfx_font_spleen.c \
  src/gfx/gfx_palette.c src/gfx/gfx_bmp.c src/gfx/gfx_bmp_io.c src/gfx/vbe_pick.c src/gfx/vbe_report.c src/gfx/vbe_bank.c \
  src/wm/wm_window.c src/wm/wm_frame.c src/wm/wm_dispatch.c \
  src/wm/wm_snap.c src/wm/wm_move.c \
  src/ui/ui_scroll.c src/ui/ui_path.c src/ui/ui_filedlg.c src/ui/ui_button.c src/ui/ui_hotpaint.c src/ui/ui_menu.c src/ui/ui_edit.c src/ui/ui_dialog.c \
  src/ui/ui_controls.c \
  src/shell/sh_kmove.c src/shell/icon_nav.c src/shell/sh_theme.c src/shell/sh_theme_io.c src/shell/sh_cursor.c src/shell/sh_desktop.c \
  src/shell/sh_iconpack.c \
  src/shell/sh_taskbar.c src/shell/sh_launcher.c src/shell/sh_context.c \
  src/shell/sh_shutdown.c src/shell/sh_core.c src/shell/sh_anim.c \
  src/shell/sw_core.c src/shell/sh_splash.c src/shell/sh_logo.c src/shell/sh_saver.c src/shell/sh_tooltip.c \
  src/apps/app_window.c src/apps/app_sysinfo.c src/apps/app_fileman.c \
  src/apps/app_notepad.c src/apps/app_calc.c src/apps/app_logview.c \
  src/apps/app_control.c src/apps/app_taskman.c \
  src/apps/app_paint.c src/apps/app_view.c src/apps/app_hex.c \
  src/apps/app_about.c src/apps/app_bench.c \
  src/apps/app_media.c src/apps/wav.c src/apps/app_clock.c src/apps/app_net.c \
  src/apps/app_theme.c src/apps/app_help.c \
  src/apps/app_charmap.c src/apps/app_props.c src/apps/app_solitaire.c \
  src/apps/app_console.c src/apps/app_welcome.c \
  src/apps/calc_core.c src/apps/sol_core.c src/apps/wrap_core.c src/apps/cmap_core.c src/apps/fsize_core.c src/apps/ring_core.c src/apps/fc_core.c src/apps/rev_core.c src/apps/trash_core.c src/apps/diff_core.c src/apps/grep_core.c src/apps/ren_core.c src/apps/ren_batch.c src/apps/card_draw.c src/apps/undo_core.c src/apps/app_freecell.c src/apps/mines_core.c src/apps/app_mines.c src/apps/app_reversi.c \
  src/apps/sheet_core.c src/apps/app_sheet.c \
  src/apps/write_core.c src/apps/app_write.c src/apps/office_ui.c \
  src/apps/eq_core.c src/apps/agenda_core.c src/apps/paint_core.c src/apps/hex_core.c \
  src/apps/thumb_core.c src/apps/assoc.c src/apps/hist_core.c \
  src/apps/map_core.c src/apps/app_diskuse.c src/apps/app_compare.c src/apps/app_archive.c src/apps/lzss_core.c src/apps/cz_file.c src/apps/car_core.c

# platform (host backend) + entry
APP_SRC  := src/platform/host/plat_host.c src/platform/host/snd_host.c \
  src/platform/host/net_host.c src/main.c

# unit-test sources
TEST_SRC := \
  tests/test_main.c tests/test_str.c tests/test_rect.c \
  tests/test_region.c tests/test_ini.c tests/test_install.c tests/test_snd.c \
  tests/test_color.c tests/test_capp.c tests/test_capp_loader.c \
  tests/test_net.c tests/test_stack.c tests/test_clip.c \
  tests/test_snap.c tests/test_bmp.c tests/test_mines.c tests/test_sheet.c tests/test_write.c tests/test_scroll.c tests/test_eq.c tests/test_agenda.c tests/test_paint.c tests/test_theme.c tests/test_path.c tests/test_thumb.c tests/test_assoc.c tests/test_hist.c tests/test_map.c tests/test_lzss.c tests/test_lastgood.c tests/test_cpu.c tests/test_vbe.c tests/test_sw.c tests/test_car.c tests/test_move.c tests/test_ctrl.c tests/test_font.c tests/test_mem.c tests/test_blit.c tests/test_iconnav.c tests/test_mach.c tests/test_hex.c tests/test_cal.c tests/test_le.c tests/test_home.c tests/test_calc.c tests/test_sol.c tests/test_wrap.c tests/test_fuzz.c tests/test_cmap.c tests/test_fsize.c tests/test_ring.c tests/test_fc.c tests/test_rev.c tests/test_trash.c tests/test_diff.c tests/test_grep.c tests/test_ren.c tests/test_undo.c

# The tests exercise the pure logic modules only, so they link against just
# these (no platform backend, no shell) -- keeping the test binary hermetic.
TEST_CORE_SRC := \
  src/sys/sys_str.c src/sys/sys_mem.c src/sys/sys_log.c src/sys/cpu_core.c src/sys/mach_core.c src/sys/cal_core.c src/sys/sys_le.c src/sys/sys_home.c \
  src/cfg/cfg_ini.c src/cfg/lastgood.c src/gfx/vbe_pick.c src/gfx/vbe_report.c src/gfx/vbe_bank.c src/gfx/gfx_rect.c src/install/install_core.c \
  src/sys/snd_common.c src/gfx/gfx_palette.c src/capp/capp.c \
  src/capp/capp_loader.c \
  src/net/net_util.c src/net/net_stack.c src/sys/clip.c src/wm/wm_snap.c src/wm/wm_move.c src/shell/icon_nav.c \
  src/gfx/gfx_surface.c src/gfx/gfx_bmp.c src/apps/calc_core.c src/apps/sol_core.c src/apps/wrap_core.c src/apps/cmap_core.c src/apps/fsize_core.c src/apps/ring_core.c src/apps/fc_core.c src/apps/rev_core.c src/apps/trash_core.c src/apps/diff_core.c src/apps/grep_core.c src/apps/ren_core.c src/apps/card_draw.c src/apps/undo_core.c src/apps/mines_core.c \
  src/apps/sheet_core.c src/apps/write_core.c src/apps/eq_core.c src/apps/agenda_core.c \
  src/apps/paint_core.c src/apps/hex_core.c src/apps/thumb_core.c src/apps/assoc.c \
  src/apps/hist_core.c src/apps/map_core.c src/apps/lzss_core.c \
  src/apps/car_core.c \
  src/gfx/gfx_blit.c \
  src/shell/sh_theme_io.c src/shell/sw_core.c \
  src/ui/ui_scroll.c src/ui/ui_controls.c src/ui/ui_menu.c src/ui/ui_path.c src/ui/ui_button.c src/gfx/gfx_draw.c src/gfx/gfx_font.c \
  src/gfx/gfx_font_data.c src/gfx/gfx_font_spleen.c

# INSTALL.EXE core + CLI, built as a host binary too (build/install) so the
# install/uninstall flow can be exercised without DOS.
INSTALL_SRC := src/install/install_core.c src/install/install_main.c

# mkcapp: host tool that authors .CAPP packages (build/mkcapp).
MKCAPP_SRC := src/capp/capp.c src/capp/mkcapp.c src/sys/sys_le.c \
              src/sys/sys_str.c src/sys/sys_mem.c src/sys/sys_log.c

# gen_iconpack: host tool that bakes the original "Castalia" desktop icon pack
# (assets/icons/castalia/*.bmp) with the engine's own renderer. Links only the
# gfx primitives + memory helpers it needs -- no platform backend.
GENICON_SRC := tools/gen_iconpack.c \
  src/gfx/gfx_surface.c src/gfx/gfx_rect.c src/gfx/gfx_draw.c \
  src/gfx/gfx_blit.c src/gfx/gfx_bmp.c \
  src/sys/sys_mem.c src/sys/sys_str.c src/sys/sys_log.c src/sys/sys_le.c

CORE_OBJ := $(patsubst %.c,$(BUILD)/%.o,$(CORE_SRC))
APP_OBJ  := $(patsubst %.c,$(BUILD)/%.o,$(APP_SRC))
TEST_OBJ := $(patsubst %.c,$(BUILD)/%.o,$(TEST_SRC))
TEST_CORE_OBJ := $(patsubst %.c,$(BUILD)/%.o,$(TEST_CORE_SRC))
INSTALL_OBJ := $(patsubst %.c,$(BUILD)/%.o,$(INSTALL_SRC))
MKCAPP_OBJ  := $(patsubst %.c,$(BUILD)/%.o,$(MKCAPP_SRC))
GENICON_OBJ := $(patsubst %.c,$(BUILD)/%.o,$(GENICON_SRC))

CASTALIA_BIN := $(BUILD)/castalia
TESTS_BIN    := $(BUILD)/run_tests
INSTALL_BIN  := $(BUILD)/install
MKCAPP_BIN   := $(BUILD)/mkcapp
GENICON_BIN  := $(BUILD)/gen_iconpack

.PHONY: sanitize all run test demos abuse lint check clean dirs gen-iconpack gen-font gen-stats gen-presskit-logo icons-demo

all: $(CASTALIA_BIN) $(INSTALL_BIN) $(MKCAPP_BIN)

$(CASTALIA_BIN): $(CORE_OBJ) $(APP_OBJ)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $^
	@echo "  LINK  $@"

$(TESTS_BIN): $(TEST_CORE_OBJ) $(TEST_OBJ)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCS) -Itests -o $@ $^
	@echo "  LINK  $@"

$(INSTALL_BIN): $(INSTALL_OBJ)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $^
	@echo "  LINK  $@"

$(MKCAPP_BIN): $(MKCAPP_OBJ)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $^
	@echo "  LINK  $@"

$(GENICON_BIN): $(GENICON_OBJ)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $^
	@echo "  LINK  $@"

# Pattern rule: compile any tracked .c into build/<path>.o. -MMD -MP emits a
# .d sidecar listing the headers each object depends on, so editing a header
# (e.g. a struct in sh_internal.h) correctly rebuilds every dependent object
# instead of leaving stale, ABI-mismatched .o files.
$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c -o $@ $<

# Pull in the generated header-dependency files (ignored if absent).
-include $(CORE_OBJ:.o=.d) $(APP_OBJ:.o=.d) $(TEST_OBJ:.o=.d) $(TEST_CORE_OBJ:.o=.d) $(INSTALL_OBJ:.o=.d) $(MKCAPP_OBJ:.o=.d) $(GENICON_OBJ:.o=.d)

run: $(CASTALIA_BIN)
	@mkdir -p $(BUILD)/SYS $(BUILD)/LOGS
	CASTALIA_HOME=$(BUILD) $(CASTALIA_BIN) --headless --open-launcher \
	    --open-sysinfo --icons assets/icons/tango --frames 10 --shot $(BUILD)/castalia.bmp
	@echo "  SHOT  $(BUILD)/castalia.bmp"

# (Re)generate include/castalia/buildstats.h from the current source tree.
gen-stats:
	sh tools/gen_buildstats.sh

# (Re)generate the Spleen system font table from the committed BDF.
gen-font:
	python3 tools/bdf2font.py assets/fonts/spleen-5x8.bdf g_spleen_font8 \
	    > src/gfx/gfx_font_spleen.c
	@echo "  FONT  src/gfx/gfx_font_spleen.c"

# (Re)bake the original Castalia icon pack into assets/icons/castalia/.
gen-iconpack: $(GENICON_BIN)
	@mkdir -p assets/icons/castalia
	$(GENICON_BIN) assets/icons/castalia
	@echo "  ICONS assets/icons/castalia"

# Render the icon pack in use (desktop + File Manager toolbar + Quick Launch),
# for the before/after comparison. ICONS defaults to the Tango pack; override
# e.g. `make icons-demo ICONS=assets/icons/castalia`.
ICONS ?= assets/icons/tango
icons-demo: $(CASTALIA_BIN)
	@mkdir -p $(BUILD)/SYS $(BUILD)/LOGS
	CASTALIA_HOME=$(BUILD) $(CASTALIA_BIN) --headless --open-fileman \
	    --icons $(ICONS) --frames 3 --shot $(BUILD)/castalia-iconpack.bmp
	@echo "  SHOT  $(BUILD)/castalia-iconpack.bmp"

test: $(TESTS_BIN)
	@$(TESTS_BIN)

# Strict-C89 portability guard for the Open Watcom DOS build (catches C99-isms
# the host's gnu89 accepts but Watcom rejects). Runs without a DOS toolchain.
gen-logo: $(BUILD)/gen_logo
	@mkdir -p $(BUILD)/logo
	@$(BUILD)/gen_logo $(BUILD)/logo

# The press kit's crest icons and favicon, from presskit/castalia-98-logo.png --
# the same source the icon packs' logo-*.bmp are baked from. They used to come
# from gen-logo above, which bakes sh_logo.c's PROCEDURAL drawing: correct while
# that drawing was the mark, and stale from the moment the shell started
# preferring the pack image. The desktop and the press kit showed two different
# castles and nothing said so, because both were generated correctly from
# different places.
gen-presskit-logo:
	@python3 tools/gen_presskit_logo.py

$(BUILD)/gen_logo: tools/gen_logo.c src/shell/sh_logo.c
	@mkdir -p $(BUILD)
	@$(CC) $(CFLAGS) -Isrc/shell -o $@ tools/gen_logo.c src/shell/sh_logo.c \
	  src/gfx/gfx_surface.c src/gfx/gfx_draw.c src/gfx/gfx_blit.c \
	  src/gfx/gfx_rect.c src/gfx/gfx_bmp.c src/gfx/gfx_bmp_io.c \
	  src/gfx/gfx_font.c src/gfx/gfx_font_data.c src/gfx/gfx_font_spleen.c \
	  src/gfx/gfx_palette.c src/ui/ui_button.c \
	  src/sys/sys_mem.c src/sys/sys_log.c src/sys/sys_str.c src/sys/sys_time.c \
	  src/platform/host/plat_host.c
	@echo "  CC    $@"

# End-to-end scenes: every scripted demo, checked for MISMATCH. `test` proves
# the logic; this proves the wiring between a click and what it changes.
demos: $(CASTALIA_BIN)
	@BIN=$(CASTALIA_BIN) sh tools/run_demos.sh

# Boot the shell against deliberately hostile input and require it to recover.
abuse: $(CASTALIA_BIN)
	@sh tools/run_abuse.sh

# Memory errors -- the one class every other gate here is blind to. A program
# can read past a buffer, write one byte too far, or branch on an
# uninitialised value and still produce exactly the right answer, so the unit
# tests, the demos and the abuse worlds all pass. Skips itself with a note if
# valgrind is not installed, rather than failing a machine that lacks it.
#
# tools/check_memcheck_control.sh proves this can fail; it is separate because
# it rebuilds the tree twice.
memcheck: $(CASTALIA_BIN) $(TESTS_BIN)
	@sh tools/run_memcheck.sh

# The unit tests again under ASan + UBSan + LeakSanitizer. Not redundant with
# memcheck: valgrind puts no redzones around globals and runs here with
# --leak-check=no, so the two tools see different things. It builds into
# build-san/ and leaves the ordinary build alone.
sanitize:
	@CC=$(CC) sh tools/run_sanitize.sh

# Everything, in one command, stopping at the first failure. Running the gates
# separately is how a lint failure gets read past and committed over.
check:
	@$(MAKE) --no-print-directory test
	@$(MAKE) --no-print-directory lint
	@$(MAKE) --no-print-directory demos
	@$(MAKE) --no-print-directory abuse
	@$(MAKE) --no-print-directory memcheck
	@$(MAKE) --no-print-directory sanitize
	@echo "All checks passed."



lint:
	@CC=$(CC) sh tools/c89_lint.sh
	@CC=$(CC) sh tools/check_dos_syntax.sh
	@CC=$(CC) sh tools/check_second_compiler.sh
	@CC=$(CC) sh tools/check_warning_sweep.sh
	@sh tools/check_dos_build.sh
	@sh tools/check_ctrl_keys.sh
	@sh tools/check_demos_listed.sh
	@sh tools/check_icon_slots.sh
	@sh tools/check_cmd_ids.sh
	@sh tools/check_menu_wired.sh
	@sh tools/check_clip.sh
	@sh tools/check_wheel.sh
	@sh tools/check_minimize.sh
	@sh tools/check_docs.sh


clean:
	rm -rf $(BUILD)
	@echo "  CLEAN"
