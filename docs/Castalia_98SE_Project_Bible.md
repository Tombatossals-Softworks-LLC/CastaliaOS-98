# Castalia 98 SE: Project Bible for Dave Nebot

Castalia 98 SE is not a plan to copy Microsoft Windows 98 SE. It is a plan to build an original, legally clean, Win9x-inspired retro computing environment for the IBM Pentium II 400 MHz class machine described by Dave Nebot. The project deliberately separates three things that are often blurred in retro projects: the operating base, the visual shell, and the compatibility layer. The practical v1 target is a DOS-hosted 32-bit graphical environment that boots from FreeDOS, starts automatically, presents a polished Windows-98-era desktop metaphor, and runs original Castalia applications plus DOS programs. A secondary compatibility target can provide a Windows 98 SE replacement shell for users who already own and install Windows 98 SE legally. A true kernel is treated as a research track, not the first production path.

The machine is strong for the era: Pentium II at 400 MHz, 128 MB of PC100 SDRAM, Intel 440BX-era platform, IDE storage, PS/2 input, integrated S3 Trio3D-class graphics, integrated business audio, USB 1.1, Ethernet, and PCI/ISA expansion. The target is not merely nostalgia. The target is to make a machine of that period feel curated, coherent, stable, and personal, with a desktop that behaves like a small operating environment rather than a pile of loose DOS utilities.


## Research Notes and Source Basis

- [S1] FreeDOS Project: FreeDOS describes itself as an open source DOS-compatible operating system for classic DOS games, legacy business software, and new DOS programs. URL: https://www.freedos.org/
- [S2] ReactOS Download Page: ReactOS 0.4.15 is available, but the project warns that ReactOS is still alpha and does not guarantee stability or safety of files. URL: https://reactos.org/download/
- [S3] Open Watcom C/C++ Getting Started: Open Watcom supports development and debugging for DOS, extended DOS, Windows 3.x, Windows 95/98/Me, Win32s, Windows NT/2000/XP, OS/2, and related x86 targets. URL: https://openwatcom.org/ftp/manuals/1.5/c_readme.pdf
- [S4] Allegro 4 DOS Specifics: Allegro 4 includes DOS-specific support areas for graphics, digital sound, keyboard, mouse, joystick, and MIDI drivers. URL: https://liballeg.org/stabledocs/en/alleg036.html
- [S5] QEMU i440fx PC documentation: QEMU pc-i440fx emulates an i440FX host bridge, PIIX3 PCI-to-ISA bridge, PS/2 keyboard and mouse, PCI IDE, floppy, network adapters, and serial ports. URL: https://www.qemu.org/docs/master/system/i386/pc.html
- [S6] Bochs Current Release: Bochs binary packages list PCI chipsets including i430FX, i440FX, and i440BX, plus Bochs VBE, Cirrus SVGA, Voodoo models, SB16, ES1370, NE2000, E1000, and USB controllers. URL: https://bochs.sourceforge.io/getcurrent.html
- [S7] IBM PC 300PL specification sheet: IBM PC 300PL documentation lists ports and devices including video, serial, enhanced parallel, USB, 32X-14X CD-ROM options, 10/100 Ethernet, ATA-33 Enhanced IDE, audio jacks, and Wake on LAN. URL: https://ps-2.kev009.com/pccbbs/commercial_desktop/dts6202f.pdf
- [S8] Windows 98 SE system requirements listing: A Windows 98 SE retail CD listing reports requirements such as a 486DX/66 MHz or higher CPU, 24 MB of RAM, FAT16 or FAT32 disk space ranges, CD-ROM or DVD-ROM drive, VGA or higher display, and a compatible mouse. URL: https://archive.org/details/win98se_202001


## 1. Executive Decision

Build Castalia 98 SE as an original retro desktop operating environment with two official modes.

Mode A, called Castalia DOS Shell, is the flagship product. It boots through FreeDOS, loads a 32-bit protected-mode executable, owns the graphical session, exposes a window manager, file manager, taskbar, settings center, DOS application launcher, and first-party applications. This is the safest, most achievable, and most coherent path.

Mode B, called Castalia Win98 Shell, is a compatibility harness. It runs on a legal Windows 98 SE installation as an alternate shell or desktop suite. It is useful for rapid UI prototyping, for running Win32-era utilities, and for giving the machine a Castalia identity without reimplementing a full Win9x kernel. It must not redistribute Microsoft files, bypass activation or licensing, or copy Microsoft artwork.

Mode C, called Castalia Native Kernel, is a long-term research track. It is allowed to exist as a bootloader and kernel experiment, but it must not block Mode A. A real Win9x-compatible kernel, VxD driver ecosystem, GDI, USER, shell, registry, networking stack, multimedia stack, setup program, and device model would be a multi-year project for a team, not a first milestone for a single sprint. ReactOS is the cautionary lighthouse here: even a long-running open-source Windows-compatible OS project is still presented as alpha by its own download page [S2].

Primary order of battle:
1. Create the DOS-hosted product first.
2. Use emulation to develop safely.
3. Validate on the IBM Pentium II target early.
4. Keep the native kernel track as a separate laboratory.
5. Never copy Microsoft code, Microsoft binaries, Microsoft icons, Microsoft sounds, Microsoft branding, or leaked internals.


## 2. Non-Negotiable Product Goals

The project must deliver a usable, charming, technically honest retro system. It should feel like a lost 1999 professional desktop made by a small European engineering team with taste, discipline, and a mildly dangerous affection for beveled rectangles.

Product goals:
- Boot to a graphical desktop without manual command typing.
- Run comfortably on Pentium II 400 MHz with 128 MB RAM.
- Provide 640x480 8 bpp fallback and 800x600 16 bpp preferred mode.
- Use original artwork, original icons, original fonts where licensing allows, and original sound effects.
- Provide 16-color and 256-color theme pipelines for retro authenticity.
- Provide a file manager with copy, move, delete, rename, view details, open with, and drive navigation.
- Provide a DOS program launcher with profile-based memory and path settings.
- Provide a system control center for display, mouse, keyboard, sound, theme, boot options, and safe mode.
- Store settings in transparent, repairable INI files or a simple registry-like database.
- Include a crash log and a last-good-configuration fallback.
- Support real hardware testing, not only emulator screenshots.
- Build from source with a documented toolchain.
- Keep the source tree small enough to reason about but modular enough to grow.

The design target is not a perfect clone. The design target is a retro operating environment with professional finish: predictable behavior, fast redraw, clear affordances, recoverable failures, and delight without circus paint.


## 3. Legal and Ethical Scope

This project must remain legally clean.

Allowed:
- Original C or C++ code.
- Open-source components with compatible licenses, if attribution and redistribution terms are obeyed.
- FreeDOS as a bootable DOS-compatible base where licensing permits [S1].
- Open Watcom as a retro-appropriate compiler toolchain where its license and redistribution requirements are obeyed [S3].
- Original UI assets inspired by late-1990s design language, not copied from Microsoft.
- A replacement shell for a user's own legal Windows 98 SE installation, provided the project does not redistribute Windows files.
- A compatibility layer based on documented or independently implemented APIs.

Forbidden:
- Shipping Microsoft Windows 98 SE files, system DLLs, fonts, icons, cursors, sounds, setup files, boot disks, product keys, or registry hives.
- Using leaked Microsoft source code or leaked symbol information.
- Presenting the product as Windows, Windows 98, Windows 98 SE, or a Microsoft product.
- Copying the Start button, Windows flag, system icons, startup sounds, wallpaper, branding, or exact UI artwork.
- Patching Windows binaries for redistribution.
- Bundling abandonware without a license.

Naming guidance:
- Use Castalia 98 SE as a project codename only if the final public name avoids confusion with Microsoft products.
- Public name suggestion: Castalia Desktop 98, Castalia Retro Environment, or Castalia OS/386-PII Edition.
- The phrase Win9x-inspired is acceptable in documentation. The phrase Windows 98 clone should be avoided in public material.


## 4. Hardware Target Profile

The primary hardware target is an IBM business desktop or minitower in the Pentium II 400 MHz class with Intel 440BX-era behavior, 128 MB PC100 SDRAM, IDE disk, CD-ROM, PS/2 keyboard and mouse, integrated S3 Trio3D-class video with 4 MB VRAM, integrated audio, Intel Pro/100-class Ethernet, USB 1.1, serial, parallel, PCI, and ISA expansion.

Important implications:
- CPU budget is comfortable for a 2D desktop, but not for wasteful alpha blending everywhere.
- 128 MB RAM is luxurious for DOS-era software but small by modern habits.
- 4 MB VRAM is enough for 800x600 at 16 bpp with room for back buffers only if memory is managed carefully.
- S3 Trio3D-era graphics should be treated first through VESA BIOS services rather than custom acceleration.
- IDE disks may be old and fragile. The project should support CompactFlash or SD-to-IDE replacements, but must not require them.
- USB 1.1 is not a safe primary input or storage dependency for v1.
- PS/2 input must be first-class.
- The integrated sound may be Crystal, ESS, or another business audio chip. Sound support must be optional and not required for boot.
- Ethernet is useful for later file transfer and diagnostics, but networking should not be a v1 boot dependency.

IBM PC 300PL-era documentation lists a cluster of business desktop features that map well to this plan, including video, serial, enhanced parallel, USB, CD-ROM options, 10/100 Ethernet, ATA-33 Enhanced IDE, audio jacks, and Wake on LAN [S7].


## 5. Feasibility Model

The project has three feasibility bands.

Band 1, high feasibility:
- DOS-hosted graphical desktop.
- Window manager.
- File manager.
- Settings panel.
- Built-in applications.
- DOS launcher.
- Installer that writes CONFIG.SYS and AUTOEXEC.BAT.
- Theme system.
- Real hardware boot.

Band 2, medium feasibility:
- Long filename support through optional helper layer.
- Sound mixer through Sound Blaster-compatible path or Allegro drivers.
- Basic networking through packet driver or mTCP integration.
- App package format.
- Plugin ABI.
- Crash recovery and safe mode.
- Simple cooperative multitasking.

Band 3, low feasibility for v1:
- Native Win32 binary compatibility.
- Windows 9x VxD compatibility.
- Full registry semantics.
- Plug and Play equivalent.
- Direct3D compatibility.
- USB mass storage stack.
- Preemptive process isolation.
- Full protected kernel with paging, virtual memory, and driver model.

The project must not pretend Band 3 is v1. That road leads to the bog of heroic lies. The artifact must be excellent because the scope is disciplined, not because the README shouts louder than the code.


## 6. Recommended Architecture

Recommended v1 architecture: FreeDOS + DPMI/protected-mode Castalia runtime + VESA graphics + original UI toolkit + first-party applications.

Layer map:

Layer 0: Boot substrate
- BIOS initializes hardware.
- FreeDOS boots from FAT16 or FAT32 partition.
- CONFIG.SYS loads only essential memory and device helpers.
- AUTOEXEC.BAT starts CBOOT.EXE or CASTALIA.EXE.
- Safe mode can bypass graphics and drop to DOS.

Layer 1: Host abstraction
- DOS file I/O wrapper.
- Memory allocation wrapper.
- Timer wrapper.
- Keyboard wrapper.
- Mouse wrapper.
- VESA mode wrapper.
- DOS process launcher.
- Optional packet driver wrapper.

Layer 2: System runtime
- Logger.
- Configuration service.
- Resource manager.
- Message queue.
- Cooperative scheduler.
- Error boundary and crash dialog.
- Last-good-session manager.

Layer 3: Graphics and window manager
- VESA surface.
- Back buffer.
- Dirty rectangle compositor.
- Software cursor.
- Z-order manager.
- Window decorations.
- Modal dialogs.
- Controls.
- Font renderer.
- Icon renderer.
- Theme renderer.

Layer 4: Shell
- Desktop.
- Taskbar.
- Launcher menu.
- Tray/status area.
- File manager.
- Control center.
- Run dialog.
- Shutdown dialog.
- DOS launcher.

Layer 5: Applications
- Notepad.
- Calculator.
- Paint-lite bitmap editor.
- System information.
- Disk tools front end.
- Theme editor.
- Log viewer.
- Help viewer.

Layer 6: Compatibility and extensions
- DOS app profiles.
- Optional Windows 98 shell mode.
- Optional networking tools.
- Optional sound support.
- Experimental native-kernel branch.


## 7. Toolchain Recommendation

Primary compiler: Open Watcom C/C++.

Reasoning:
- It is historically appropriate for DOS and Win9x-class development.
- It supports 16-bit and 32-bit targets including DOS, extended DOS, Windows 3.x, Windows 95/98/Me, Win32s, Windows NT/2000/XP, and OS/2 according to its own documentation [S3].
- It includes a linker, debugger, resource tools, and profiling/debugging utilities that fit the era.

Secondary tools:
- NASM or JWasm for low-level assembly only when needed.
- Python on the modern development host for asset conversion, image packing, palette reduction, manifest validation, and disk image generation.
- QEMU for quick boot smoke tests. QEMU pc-i440fx provides an i440FX/PIIX3 PC model with PS/2, IDE, floppy, network, and serial support [S5].
- Bochs for slower but useful hardware-level debugging, especially because current Bochs builds list i440BX, Bochs VBE, Cirrus SVGA, Voodoo models, SB16, ES1370, NE2000, E1000, and USB controller support [S6].
- 86Box or PCem for period-feel hardware validation, especially before touching the real IBM machine.

Optional library track:
- Allegro 4 can accelerate the early MVP because its documentation includes DOS-specific graphics, digital sound, keyboard, joystick, mouse, and MIDI support categories [S4]. However, Allegro must be wrapped behind Castalia's own platform interface so the project can replace it with direct VESA and direct input later.

Toolchain principle:
Every external dependency must sit behind a Castalia interface. The project must never become a thin theme on top of a library. Castalia owns the product shape.


## 8. Graphics Architecture

Graphics target modes:
- Minimum: 640x480 at 8 bpp, 256-color palette.
- Preferred: 800x600 at 16 bpp.
- Optional: 1024x768 at 8 bpp for S3 Trio3D-class cards if VESA mode is stable.

Why 800x600 16 bpp is the sweet spot:
- It looks professional on a 17-inch CRT.
- It allows richer icons and smoother gradients than pure 256-color mode.
- Framebuffer memory is roughly 960 KB for one 800x600x16bpp surface. Double buffering consumes roughly 1.9 MB. With 4 MB VRAM, this is plausible, but triple buffering and large offscreen caches are not.

Renderer architecture:
- Maintain one primary back buffer in conventional host memory or linear framebuffer strategy depending on mode stability.
- Use dirty rectangles for repaint.
- Clip every control and window to its visible region.
- Avoid full-screen repaints except during mode switch, theme switch, or crash recovery.
- Use software cursor with background save/restore.
- Use no per-pixel alpha in v1 except optional precomputed masks for icons and cursor.
- Use palette discipline for 8 bpp mode.

Primitive API:
- gfx_set_mode(width, height, bpp)
- gfx_present(rect_list)
- gfx_fill_rect(rect, color)
- gfx_frame_rect(rect, c1, c2)
- gfx_blit(src, dst, mask_mode)
- gfx_draw_text(font, x, y, text, color, flags)
- gfx_measure_text(font, text)
- gfx_set_clip(rect)
- gfx_capture_rect(rect, buffer)
- gfx_restore_rect(rect, buffer)

Icon system:
- Master icon source: high-quality PNG on development host.
- Exported runtime formats: 16x16, 24x24, 32x32, 48x48.
- Color tiers: 16-color, 256-color, 16-bit.
- Use ordered dithering and manual cleanup for 16-color versions.
- Use original Castalia shield/castle visual identity, not Windows flag identity.

Font system:
- Use bundled bitmap fonts only if their license permits redistribution.
- Otherwise generate original bitmap fonts.
- Required faces: system 8, system 10, title 12, mono 8, mono 10.
- Store glyph metrics in compact binary tables.


## 9. Window Manager Specification

Window manager responsibilities:
- Own all top-level windows.
- Manage focus.
- Manage z-order.
- Dispatch mouse and keyboard events.
- Provide modal dialogs.
- Provide window movement and resizing.
- Enforce clipping.
- Track invalid regions.
- Draw non-client areas.
- Coordinate taskbar entries.

Window states:
- Normal.
- Minimized.
- Maximized.
- Modal.
- Disabled owner.
- Hidden.
- Closing.

Non-client frame:
- 2 px outer frame.
- Title bar 18 px at 640x480, 20 px at 800x600.
- Close button, minimize button, maximize button.
- Active and inactive title themes.
- Optional system menu icon.

Message model:
- MSG_CREATE
- MSG_DESTROY
- MSG_PAINT
- MSG_SIZE
- MSG_MOVE
- MSG_MOUSEMOVE
- MSG_LBUTTONDOWN
- MSG_LBUTTONUP
- MSG_LBUTTONDBLCLK
- MSG_KEYDOWN
- MSG_KEYUP
- MSG_COMMAND
- MSG_TIMER
- MSG_FOCUS_GAINED
- MSG_FOCUS_LOST
- MSG_CLOSE

Controls:
- Button.
- Checkbox.
- Radio button.
- Text field.
- Multiline text box.
- List box.
- Tree view.
- Menu bar.
- Context menu.
- Toolbar.
- Tab control.
- Slider.
- Progress bar.
- Status bar.
- Group box.

Acceptance criteria:
- Dragging a window never corrupts the desktop behind it.
- Opening and closing 50 windows in sequence leaks no measurable heap beyond expected caches.
- Keyboard focus is visually obvious.
- Modal dialogs block their owner but not the entire runtime unless specified.
- Repaint remains responsive on the Pentium II target.


## 10. Shell Specification

Desktop:
- Background bitmap or solid color.
- Icons arranged on a grid.
- Drives, system folder, documents, trash, and optional launcher shortcuts.
- Right-click context menu.
- Drag selection rectangle.
- Rename inline.
- Persistent icon positions.

Taskbar:
- Left launcher button using Castalia branding.
- Running task buttons.
- Status area.
- Clock.
- Optional CPU or memory meter.
- Support bottom position for v1; left/right/top can be future.

Launcher menu:
- Programs.
- Documents.
- Settings.
- Run.
- Help.
- Restart Shell.
- Exit to DOS.
- Shutdown.

File manager:
- Two modes: single-pane explorer and dual-pane commander.
- Drive selector.
- Path bar.
- Details view.
- Icon view.
- File operations with confirmation and progress.
- Viewers for text, bitmap, and log files.
- Open-with profiles.
- Hidden/system file toggle.
- Safe delete to trash folder where possible.

Control center:
- Display.
- Theme.
- Mouse.
- Keyboard.
- Sound.
- Boot.
- DOS profiles.
- System info.
- Date and time.
- Maintenance.

Shutdown model:
- Exit to DOS.
- Restart shell.
- Reboot machine through BIOS call or safe software reset where reliable.
- Power off only when hardware and APM support are verified.


## 11. Application Model

For v1, choose simplicity over fantasy.

MVP app strategy:
- First-party apps are linked into the main executable or built as tightly controlled modules loaded by the shell.
- External DOS programs launch as child processes after the graphics session is suspended or minimized into a text-mode launcher screen.
- App isolation is cooperative, not kernel-enforced.

Package format, v2:
- File extension: .CAPP
- Internally: directory or packed file with manifest.ini, app executable/module, resources, icon set, help file, and uninstall metadata.

manifest.ini example:
[Application]
Name=Castalia Notepad
Id=com.castalia.notepad
Version=1.0.0
Entry=NOTEPAD.CXE
Icon=icons/notepad.cic
MinShell=1.0
RequiresGraphics=1
RequiresSound=0

[Resources]
Pak=resources.pak
Help=help/notepad.chm-lite

[Permissions]
FileSystem=UserPrompt
Network=None

Runtime API categories:
- sys: logging, memory, time, configuration, error reporting.
- gfx: surfaces, colors, fonts, icons, clipping.
- win: windows, messages, timers, focus, menus.
- ui: common controls.
- fs: file operations, paths, metadata, enumeration.
- proc: DOS program launch and shell restart.
- snd: optional sound playback.

Do not implement a fake Win32 API in v1. Instead, implement a Castalia API that feels natural and small.


## 12. Storage and Configuration

Filesystem:
- Use DOS and FreeDOS file services for v1.
- Use FAT16 or FAT32 depending on disk size and BIOS support.
- Do not require long filenames for core boot.
- Treat long filename support as optional enhancement.

Directory layout:
C:\CASTALIA  BIN    CASTALIA.EXE
    CBOOT.EXE
    CCFG.EXE
  SYS    CASTALIA.INI
    REGISTRY.CDB
    LASTGOOD.CFG
    HARDWARE.LOG
  DRV    VESA.DRV
    MOUSE.DRV
    KEYB.DRV
    SND_SB16.DRV
  APPS    NOTEPAD    CALC    PAINT    SYSINFO  THEMES    CLASSIC    CASTLE    HIGHCON  FONTS  ICONS  HELP  TEMP  LOGS  TRASH
Configuration files:
- CASTALIA.INI for boot and shell options.
- THEME.INI per theme.
- APPS.INI for launcher entries.
- PROFILES.INI for DOS program profiles.
- REGISTRY.CDB optional binary key-value store once INI files become too slow.

Design principle:
The system must be repairable from DOS with a text editor. If a setting breaks boot, the user must be able to delete or edit one file and recover.


## 13. DOS Program Launcher

A retro desktop without DOS launch discipline becomes a trapdoor. The launcher must treat DOS execution as a first-class scenario.

Launcher flow:
1. User selects a DOS profile.
2. Shell saves session state.
3. Shell switches to text mode or a minimal launch screen.
4. Environment variables are applied.
5. Working directory is changed.
6. Program is launched.
7. On exit, launcher captures exit code.
8. Graphics mode is restored.
9. Shell shows result if needed.

Profile fields:
- Name.
- Path.
- Working directory.
- Arguments.
- Memory notes.
- Requires EMS.
- Requires XMS.
- Requires CD-ROM.
- Requires sound.
- Pre-run batch lines.
- Post-run batch lines.
- Preferred exit behavior.

Launcher acceptance criteria:
- A failed DOS launch cannot crash the shell permanently.
- If graphics restoration fails, system drops to DOS with a clear message.
- Profiles are plain text and user-editable.
- The last 20 launches are logged.


## 14. Boot and Installer

Boot sequence:
- BIOS starts DOS from C:.
- CONFIG.SYS loads minimal memory manager and optional drivers.
- AUTOEXEC.BAT sets PATH and CASTALIA_HOME.
- CBOOT.EXE checks safe-mode flag, last crash flag, display mode preference, and minimum files.
- CBOOT.EXE starts CASTALIA.EXE.
- CASTALIA.EXE initializes logging, graphics, input, configuration, resource cache, shell, and desktop.

Installer responsibilities:
- Verify DOS or FreeDOS environment.
- Verify disk free space.
- Create C:\CASTALIA tree.
- Copy files.
- Backup existing CONFIG.SYS and AUTOEXEC.BAT.
- Add boot entries without destroying user content.
- Offer safe mode key.
- Create uninstall script.
- Create first-run hardware report.

Required installer modes:
- Full install from CD or directory.
- Upgrade install.
- Repair install.
- Portable install.
- Uninstall.

First-run wizard:
- Choose video mode.
- Choose mouse sensitivity.
- Choose keyboard layout.
- Choose theme.
- Confirm sound test.
- Create DOS games/programs folder shortcuts.

Safety:
- Never overwrite CONFIG.SYS or AUTOEXEC.BAT without creating timestamped backups.
- If boot fails twice, CBOOT should offer safe mode.


## 15. Windows 98 SE Shell Mode

This mode is not the core OS. It is a companion track for rapid prototyping and for users with a legal Windows 98 SE installation.

Implementation idea:
- Build CASTSHL.EXE as a Win32 program compatible with Windows 98 SE.
- It can be launched manually at first.
- Later, it can be configured as an alternate shell through documented Windows configuration, but the installer must ask explicitly and provide a restore path.
- It must use original Castalia icons, controls, wallpapers, and sounds.
- It must not replace system DLLs.
- It must not patch Explorer or Windows binaries.

Use cases:
- Fast UI iteration using Win32 GDI.
- Testing the Castalia visual language on the real IBM machine before DOS-native renderer completion.
- Providing a polished retro desktop suite to run beside real Windows 98 SE.

Risks:
- Users may confuse it with a Windows distribution.
- Shell replacement can strand users if a bad build starts at login.
- Windows 98 SE system components vary by driver state.

Hard rules:
- Always create a restore batch file.
- Always document how to revert from DOS.
- Treat it as a prototype and companion, not the flagship.


## 16. Native Kernel Research Track

Native kernel work must be isolated in /kernel-lab and must not block the DOS-hosted desktop.

Possible milestones:
- Stage 1 boot sector that prints text.
- Stage 2 loader that enters protected mode.
- GDT and IDT initialization.
- Basic interrupt handling.
- PIT timer.
- PS/2 keyboard input.
- Physical memory map.
- Simple heap.
- VESA mode setup through BIOS before protected mode or via VBE thunking.
- FAT12 floppy reader.
- FAT16/FAT32 reader.
- Cooperative task scheduler.
- Userland ABI experiment.

Non-goals for the research track until much later:
- Win32 binary compatibility.
- VxD driver compatibility.
- Plug and Play.
- USB stack.
- Full TCP/IP stack.
- DirectX.
- SMP.

Kernel philosophy:
The research kernel is a forge, not the town. It makes tools and knowledge. The DOS-hosted desktop is the town people can actually live in.


## 17. Performance Budget

Target machine: Pentium II 400 MHz, 128 MB RAM, 4 MB VRAM.

Startup targets:
- From AUTOEXEC to visible desktop: under 10 seconds on a healthy IDE or CF card.
- Cold graphics initialization: under 2 seconds after DOS handoff.
- First-run wizard: under 3 seconds to first screen.

Interactive targets:
- Mouse cursor: no visible lag.
- Menu open: under 100 ms.
- Window drag: visually continuous, even if outline-drag fallback is needed.
- File manager opens C:\ in under 1 second for typical directory counts.
- Theme switch under 5 seconds.
- Notepad opens under 500 ms.

Memory targets:
- Shell idle heap under 8 MB.
- Full desktop with file manager and control center under 16 MB.
- Resource cache cap configurable, default 4 MB.
- Logs rotate at 512 KB each.

Graphics targets:
- 640x480 full repaint under 100 ms.
- 800x600 dirty repaint for common UI interactions under 33 ms.
- Avoid continuous animation unless optional.

Fallback behavior:
- If 800x600x16 fails, try 640x480x16.
- If 16 bpp fails, try 640x480x8.
- If all graphics modes fail, log and return to DOS.


## 18. Repository Structure

/castalia98
  /docs
    PROJECT_BIBLE.md
    ARCHITECTURE.md
    HARDWARE_TARGETS.md
    BUILDING.md
    TESTING.md
    LEGAL.md
  /tools
    /asset_pipeline
    /disk_image
    /font_builder
    /palette_tools
  /src
    /boot
      cboot.c
    /platform
      dos_file.c
      dos_mem.c
      dos_time.c
      vesa.c
      mouse.c
      keyboard.c
      sound_null.c
    /runtime
      log.c
      config.c
      resource.c
      scheduler.c
      message.c
      crash.c
    /gfx
      surface.c
      rect.c
      blit.c
      font.c
      icon.c
      palette.c
    /wm
      window.c
      frame.c
      focus.c
      zorder.c
      invalid.c
      dispatch.c
    /ui
      button.c
      textbox.c
      listbox.c
      menu.c
      tree.c
      dialog.c
    /shell
      desktop.c
      taskbar.c
      launcher.c
      fileman.c
      control.c
      shutdown.c
    /apps
      notepad.c
      calc.c
      paint.c
      sysinfo.c
      logview.c
    /compat
      dos_launcher.c
      profiles.c
    /main.c
  /assets
    /icons_src
    /icons_runtime
    /themes
    /fonts
    /sounds
  /tests
    /unit
    /integration
    /golden
  /emulators
    qemu_run.bat
    bochs_castalia.bxrc
  /dist
    /cdroot
    /floppy
  Makefile
  README.md


## 19. Coding Standards

Language:
- C89-compatible C for core runtime.
- Limited C++ only if Open Watcom compatibility is proven and the code remains simple.
- Assembly only for small hardware operations that cannot be expressed safely in C.

Style:
- Explicit ownership of memory.
- No hidden global mutation except registered system services.
- No unbounded string copies.
- Fixed-size buffers must carry their length.
- All file operations return explicit error codes.
- Every module has init and shutdown symmetry.
- Every public function belongs to a named subsystem prefix.

Naming:
- sys_, gfx_, wm_, ui_, sh_, fs_, cfg_, res_, app_ prefixes.
- Types use PascalCase with subsystem prefix, for example GfxSurface.
- Constants use uppercase, for example CASTALIA_MAX_WINDOWS.

Error handling:
- No silent failure.
- Log subsystem, code, file, line, human message.
- User-facing errors must offer recovery when possible.
- Fatal graphics error drops to DOS with instructions.

Documentation:
- Every subsystem has a README.
- Every public API has a short comment.
- Every binary file format has a spec.
- Every build step is reproducible from a clean checkout.


## 20. Build System

Build requirements:
- One-command clean build on modern Windows or Linux host where possible.
- One-command retro build under DOS or Windows 98 SE as a stretch target.
- Deterministic asset pipeline.
- Build artifacts separated from source.

Build outputs:
- CASTALIA.EXE
- CBOOT.EXE
- INSTALL.EXE
- SYSINFO.EXE optional command-line diagnostic.
- Runtime assets.
- CD root directory.
- Optional bootable disk image.
- Checksums.

Build commands, conceptual:
make clean
make tools
make assets
make dos
make dist
make image
make test

Tool-generated files:
- Runtime icon packs.
- Font packs.
- Theme binary packs.
- Help index.
- App manifests.

Release checklist:
- Build from clean checkout.
- Run unit tests.
- Run emulator smoke test.
- Install in fresh FreeDOS image.
- Boot to desktop.
- Launch and exit Notepad, Calculator, File Manager, Control Center.
- Launch a DOS program profile.
- Verify logs.
- Verify safe mode.
- Verify uninstall.


## 21. Testing Strategy

Testing must happen in layers.

Layer 1, host unit tests:
- INI parser.
- Rectangle clipping.
- Dirty region merging.
- Path normalization.
- Manifest parsing.
- Resource pack reading.
- String functions.
- Menu layout.
- Window hit testing.

Layer 2, emulator integration:
- Boot from disk image.
- Start shell.
- Switch video mode.
- Move mouse.
- Open menu.
- Open file manager.
- Copy test file.
- Launch DOS command.
- Return to shell.
- Exit to DOS.

Layer 3, visual golden tests:
- Render desktop in known resolution.
- Capture framebuffer in emulator where possible.
- Compare against golden screenshots with tolerance.
- Verify 16-color, 256-color, and 16-bit theme exports.

Layer 4, real hardware tests:
- Boot on IBM target.
- Test PS/2 keyboard and mouse.
- Test VESA modes.
- Test file operations on IDE disk.
- Test CD-ROM read.
- Test integrated audio if implemented.
- Test PCI/ISA expansion cards where available.

Layer 5, abuse tests:
- Directory with 1,000 files.
- Very long paths where DOS allows.
- Missing theme files.
- Corrupt config.
- Failed video mode.
- Read-only files.
- Full disk.
- Interrupted copy.
- DOS program crash.
- Repeated shell restart.


## 22. Emulator Profiles

QEMU quick smoke profile:
- Machine: pc-i440fx where available.
- CPU: Pentium II-like where available.
- RAM: 128 MB.
- VGA: Cirrus or standard VBE fallback.
- Storage: IDE disk image.
- Input: PS/2.
- Sound: SB16 optional.

Example command, adjust paths locally:
qemu-system-i386 -M pc -cpu pentium2 -m 128 -hda castalia.img -cdrom freedos.iso -boot d -vga cirrus -device sb16 -serial stdio

Bochs hardware-debug profile:
- Chipset: i440BX if available.
- VGA: Bochs VBE or Cirrus SVGA.
- Sound: SB16 optional.
- Network: NE2000 optional.
- Enable logging for BIOS and device behavior.

Bochs is useful because its current binary feature list includes i440BX and multiple legacy device models [S6]. QEMU is useful because its documented i440FX PC model includes the essential PC peripherals needed for boot smoke testing [S5].

Real hardware profile:
- Record BIOS version.
- Record disk model.
- Record video BIOS and VESA modes.
- Record mouse and keyboard behavior.
- Record audio chip ID.
- Record PCI device list.
- Keep a known-good DOS boot floppy or CD nearby.


## 23. Visual Design System

Design principles:
- Period-authentic, not derivative.
- Professional, not toy-like.
- Clear depth hierarchy.
- Crisp edges.
- Conservative animation.
- Legible on CRT and LCD.
- Works in 16-color austerity and 16-bit richness.

Brand motif:
- Castle or shield identity.
- Castilian/Mediterranean stone palette translated into low-color UI.
- No Windows flag, no Microsoft-era icon copying.
- Default wallpaper: quiet stone-gradient or geometric castle silhouette.
- Accent: restrained royal blue, antique gold, and graphite gray.

Control rendering:
- Buttons use crisp bevels with one-pixel highlights and shadows.
- Active window title is strong but not radioactive.
- Inactive title is readable and calm.
- Dialogs use generous spacing by 1998 standards.
- Icons communicate through silhouette first, color second.

Theme files:
THEME.INI
[Theme]
Name=Castalia Classic
Author=Dave Nebot
ColorDepths=4,8,16

[Metrics]
TitleBarHeight=20
BorderWidth=2
MenuHeight=18
IconGrid=64

[Colors]
Desktop=#2B3A4A
Window=#C0C0C0
Text=#000000
TitleActive=#1B3F7A
TitleInactive=#6A7888
Accent=#B68A2E

[Assets]
Wallpaper=wallpaper.cbm
Icons=icons.cip
Sounds=sounds.csp


## 24. First-Party Applications

Notepad:
- Plain text editor.
- Open, save, save as.
- Word wrap.
- Find.
- Fixed font mode.
- Status bar with line and column.

Calculator:
- Standard mode.
- Programmer mode later.
- Keyboard support.

Paint-lite:
- BMP or PCX support.
- Pencil, line, rectangle, fill.
- 16-color palette mode.
- Icon-editing mode as stretch feature.

System Information:
- CPU identification where possible.
- Memory report.
- DOS version.
- VESA mode list.
- Drives.
- Environment variables.
- PCI scan where possible.
- Log export.

Control Center:
- Display, mouse, keyboard, theme, boot, sound, DOS profiles.

Help Viewer:
- Lightweight hypertext or structured plain text.
- Search index optional.
- Include recovery instructions.

Installer:
- Text-mode first.
- Graphical installer later.
- Always includes repair and uninstall.


## 25. Networking Roadmap

Networking is not v1-critical, but the IBM target has integrated 10/100 Ethernet class hardware according to the business desktop profile [S7].

Potential strategy:
- v1: no networking dependency.
- v1.5: support packet drivers where available.
- v2: integrate or interoperate with mTCP-style tools if licensing permits.
- v2: provide file transfer assistant through FTP or HTTP client.
- v3: graphical network browser only if the stack is reliable.

Use cases:
- Move files from a modern machine to the retro IBM.
- Download small packages from a local LAN server.
- Send crash logs to a local machine.

Security stance:
- This is a retro system. Do not expose it directly to the public internet.
- Prefer LAN-only transfer.
- Disable network services by default.


## 26. Sound Roadmap

Sound must be optional.

v1:
- Null sound driver.
- Beep or simple PC speaker confirmation if desired.
- UI must not require sound.

v1.5:
- Sound Blaster-compatible driver path.
- Theme sounds in low-rate WAV or VOC-like packed format.
- Startup and shutdown sounds original to Castalia.

v2:
- Detect common ESS or Crystal audio where practical.
- Provide manual driver selection.
- Add mixer volume if hardware allows.

Principle:
A failed sound init cannot prevent boot. The desktop is a ship, not a jukebox.


## 27. Risk Register

Risk: Scope explosion into full Windows clone.
Mitigation: Keep v1 as DOS-hosted original environment. Maintain clear non-goals.

Risk: Video mode instability on real S3 hardware.
Mitigation: Provide 640x480x8 fallback and text-mode safe mode.

Risk: Old hard disk failure.
Mitigation: Test on disk image and recommend backup or CF/SD-to-IDE clone before experiments.

Risk: Toolchain friction.
Mitigation: Start with host-buildable code and minimal Open Watcom target. Keep dependency wrappers.

Risk: Placeholder code shipped as if it were finished.
Mitigation: The engineering contract requires compilable source, build scripts, and no acceptance of stubs.

Risk: Asset licensing contamination.
Mitigation: Generate original assets and document sources.

Risk: Win98 Shell Mode breaks boot.
Mitigation: Manual launch first. Shell replacement only with restore batch and clear instructions.

Risk: Too much animation.
Mitigation: Performance budgets and disable animations by default.

Risk: Real hardware testing too late.
Mitigation: First bootable MVP on IBM by Phase 2.


## 28. Roadmap

Phase 0: Research and baseline
- Inventory IBM hardware.
- Install or boot FreeDOS test media.
- Confirm VESA modes.
- Confirm PS/2 mouse.
- Confirm disk and CD-ROM access.
- Create emulator profiles.
- Create design palette and icons.

Phase 1: Bootable MVP
- FreeDOS boots.
- AUTOEXEC starts Castalia.
- Graphics mode initializes.
- Mouse cursor moves.
- Desktop appears.
- Taskbar appears.
- Launcher menu opens.
- Exit to DOS works.

Phase 2: Window manager and file manager
- Top-level windows.
- Controls.
- Dialogs.
- File manager.
- Settings storage.
- Basic error handling.

Phase 3: Applications and installer
- Notepad.
- Calculator.
- System Info.
- Control Center.
- Installer and repair mode.
- DOS profiles.

Phase 4: Polish and hardware hardening
- 16-color and 256-color themes.
- Sound optional.
- Better file operations.
- Crash recovery.
- Real hardware QA matrix.

Phase 5: Package ecosystem
- .CAPP format.
- Theme editor.
- SDK headers.
- Example app.
- Documentation.

Phase 6: Experimental branches
- Win98 Shell Mode.
- Native kernel lab.
- Packet-driver networking.


## 29. Engineering Contract

Whoever implements this project holds every role at once: principal engineer,
architect, build engineer, QA lead, and technical writer.

The work must produce:
- A complete repository structure.
- Complete source files, not fragments.
- Build scripts.
- Asset pipeline scripts.
- Emulator run scripts.
- Documentation.
- Test plan.
- A clear list of what compiles now and what is planned next.

The work must not:
- Use placeholders as final code.
- Claim untested code works without saying how to test it.
- Copy Microsoft assets.
- Generate Windows product keys or installation media.
- Depend on a modern browser engine.
- Require hardware acceleration.
- Require more than 128 MB RAM for the core shell.
- Hide critical assumptions.

Decisions should be made autonomously wherever the Bible gives enough context. When ambiguity remains, choose the route that maximizes real-hardware bootability, legal cleanliness, and maintainability.


## 30. Acceptance Criteria for v1

A v1 release is acceptable only if all conditions pass.

Boot:
- Fresh FreeDOS install can start Castalia automatically.
- Safe mode works.
- Exit to DOS works.

Graphics:
- At least 640x480x8 works in emulator.
- Preferred 800x600x16 works where VESA supports it.
- Failure path returns to DOS.

Input:
- Keyboard works.
- Mouse works.
- Focus behavior is predictable.

Shell:
- Desktop icons persist.
- Taskbar updates with running windows.
- Launcher opens and starts built-in apps.
- Shutdown menu works.

File manager:
- Copy, move, delete, rename, create folder.
- Does not destroy files on failed operations.
- Logs errors.

Apps:
- Notepad opens and saves text.
- Calculator performs basic operations.
- System Info reports key environment data.
- Control Center changes at least theme and video preference.

Installer:
- Installs, repairs, upgrades, and uninstalls.
- Backs up modified files.

Documentation:
- User guide.
- Developer guide.
- Recovery guide.
- Build guide.
- Legal notice.

Performance:
- Usable on Pentium II 400 MHz.
- Idle shell remains within memory budget.
- No obvious redraw corruption in normal use.


## Appendix A: Legal Notice Draft

# Castalia 98 SE Legal Notice Draft

Castalia 98 SE is an original retro computing project. It is not Microsoft Windows, Windows 98, Windows 98 Second Edition, MS-DOS, or any Microsoft product. The project does not include Microsoft operating system files, product keys, setup media, registry hives, icons, sounds, fonts, wallpapers, or branding.

The project may provide tools that run on a user's own legally installed copy of Windows 98 SE, but it must not redistribute Microsoft components or modify Microsoft binaries for redistribution.

All Castalia source code, artwork, icons, themes, sounds, documentation, and package formats should be original or included only under licenses that permit redistribution. Every third-party component must be listed in THIRD_PARTY_NOTICES.md with license, source, version, and redistribution notes.