# DSi camcorder

Homebrew app that records video with the DSi's outer camera and turns those
recordings into animated GIFs on the console itself, no computer needed.

![Example video](resources/example1.gif)

## Getting it running

`make` builds `dsi-camcorder.nds` with [devkitARM](https://devkitpro.org/wiki/Getting_Started).
CI uploads the `.nds` and a `.cia` as build artifacts on every push, and attaches both to
GitHub releases on tags; the `.nds` is not committed to the repository.

The app talks to the camera through DSi-only hardware (`SCFG` clock gating and the
`CAM_*` registers), so it needs **DSi mode**. On a DS, DS Lite, or anything running in
DS mode the camera never initialises, and the app can do nothing. That rules out
DS-mode flashcarts - only a loader that unlocks DSi mode works.

### Flashcart (DSpico)

Copy `dsi-camcorder.nds` anywhere onto the DSpico's micro SD card and start it from Pico
Launcher. The DSpico boots as a DSi-mode cart, so the camera is available. Note the file
system the app sees is the DSpico's micro SD card, so recordings are written to
`/DCIM/100DSI00/` on that card, not the console's SD card.

The cartridge needs firmware flashed: the WRFUxxed build boots on a stock DSi/3DS, and
the hybrid build on one that already has CFW. On an original DS or a DS Lite the DSpico
is limited to DS mode, so this app cannot record there. See the
[setup guide](https://sanrax.github.io/flashcart-guides/cart-guides/dspico/).

### Custom firmware (Unlaunch)

On a CFW DSi ([dsi.cfw.guide](https://dsi.cfw.guide/)), copy `dsi-camcorder.nds` to the
console's SD card, hold `A` + `B` while booting to open Unlaunch's file menu and pick the
file. Unlaunch hands homebrew full `SCFG_EXT` access, so the camera works, and recordings
go to `/DCIM/100DSI00/` on the console's SD card.

### DSiWare

`make_cia --srl=dsi-camcorder.nds` (what CI does) wraps the build in a `.cia` that
installs as a DSiWare title on a CFW DSi via hiyaCFW or a title manager. DSiWare always
runs in DSi mode. Loading the `.nds` through nds-bootstrap instead runs it in DS mode, so
use the `.cia` if that is your loader.

## Controls

The bottom screen holds the menu, the top screen shows what the camera (or the converter)
is looking at.

- `A` records. The video is written to `/DCIM/100DSI00/VID_####.BIN` on the SD card it
  was started from, and `START` stops and saves it.
- `B` converts a recording. The browser starts in `/DCIM/100DSI00`, lists folders
  and `.BIN` files only and previews the highlighted recording on the top screen:
  - `A` opens a folder, or starts converting the recording
  - `B` goes up one folder
  - `START` goes back to the menu
- `START` in the menu quits.

The GIF is written next to the recording, `VID_0001.BIN` becomes `VID_0001.gif`. All
frames share a 256 color palette picked from the recording, and every frame keeps the
length the camera actually took to capture it, so the GIF plays back at recording
speed. If a recording carries no usable timing, the GIF is written at 10fps. `B`
cancels a conversion, which deletes the incomplete GIF.

## On a computer

The `.bin` recordings can also be converted on a computer with the scripts in this
repository. Requirements:
```
python3
ffmpeg
```

How to run:
```
./convert.sh path/to/file.bin <framerate_int>
```

![Example video](resources/example.gif)

Credits to:
- [Arisotura](http://kuribo64.net) for finding why it wasn't working
- [devkitPro](https://github.com/devkitPro) for devkitARM and libnds
- [nocash](https://problemkaputt.de) for GBATek
