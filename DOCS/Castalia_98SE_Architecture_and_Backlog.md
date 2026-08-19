# Castalia 98 SE Architecture and Backlog

## Architecture Summary

The flagship product is a FreeDOS-hosted 32-bit graphical desktop environment. It is not a Windows 98 SE clone. It is an original retro operating environment with a Win9x-era interaction model.

## Modules

### platform
- dos_file.c
- dos_mem.c
- dos_time.c
- vesa.c
- mouse.c
- keyboard.c
- sound_null.c

### runtime
- log.c
- config.c
- resource.c
- scheduler.c
- message.c
- crash.c

### gfx
- surface.c
- rect.c
- blit.c
- font.c
- icon.c
- palette.c

### wm
- window.c
- frame.c
- focus.c
- zorder.c
- invalid.c
- dispatch.c

### ui
- button.c
- textbox.c
- listbox.c
- menu.c
- tree.c
- dialog.c

### shell
- desktop.c
- taskbar.c
- launcher.c
- fileman.c
- control.c
- shutdown.c

### apps
- notepad.c
- calc.c
- sysinfo.c
- logview.c
- paint.c later

## Phase Backlog

### Phase 0: Baseline
- Inventory hardware.
- Establish FreeDOS boot.
- Confirm VESA modes.
- Confirm PS/2 input.
- Create emulator profiles.

### Phase 1: Graphical MVP
- CBOOT.EXE safe launcher.
- CASTALIA.EXE graphics init.
- Desktop background.
- Mouse cursor.
- Taskbar.
- Launcher menu.
- Exit to DOS.

### Phase 2: Window Manager
- Window structs.
- Message queue.
- Z-order.
- Dirty rectangles.
- Buttons and dialogs.
- Basic menus.

### Phase 3: File Manager and Apps
- Drive enumeration.
- Directory listing.
- Copy, move, delete, rename.
- Notepad.
- Calculator.
- System Info.

### Phase 4: Installer and Recovery
- INSTALL.EXE.
- CONFIG.SYS and AUTOEXEC.BAT backups.
- Safe mode.
- Crash logs.
- Last-good config.

### Phase 5: Polish
- Theme packs.
- 16-color and 256-color asset export.
- Optional sound.
- Help viewer.
- Documentation.

### Phase 6: Extensions
- .CAPP format.
- Packet-driver networking.
- Win98 shell companion.
- Native kernel lab.
