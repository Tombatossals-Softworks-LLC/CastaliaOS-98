# docs/media — where these pictures come from

Every image here is a real frame this project rendered. None is a mockup, and
none has been retouched.

The problem this file exists to fix is that **nothing recorded how any of them
had been made**, so when the code that draws them changed, nobody could remake
them — and several went quietly stale against the shell they were documenting.
A screenshot with no recipe is a claim you cannot re-check.

`tools/bmp2png.py` converts the BMPs the shell writes into the PNGs the docs
use (stdlib only, no Pillow or ImageMagick), so the recipes below are commands
anyone can run.

## Reproducible

| Image | Command |
|-------|---------|
| `desktop-800x600.png` | `make run` → `python3 tools/bmp2png.py build/castalia.bmp docs/media/desktop-800x600.png` |
| `context-menu.png` | `build/castalia --headless --ctx --icons assets/icons/tango --frames 10 --shot ctx.bmp` → `bmp2png.py ctx.bmp docs/media/context-menu.png` |

Note the frame count. The window-open zoom and the launcher slide-up both take
about seven frames, and they draw **over** everything while they run — a shot
taken at frame 3 catches an outline mid-flight across the picture and looks
exactly like a rendering bug. `make run` used to do that. Ten frames is past
the end of both.

## Not yet reproducible

The rest predate this tool and were captured by hand, so their exact scenes are
not recorded here. They are still real frames, but they show the shell as it
was on the day they were taken rather than as it is now; the `qemu-freedos-*`
set in particular is photographed from a real FreeDOS boot in QEMU, which no
host command can reproduce at all.

Adding a recipe row above is worth more than replacing an image without one.
