# DSi camcorder

Homebrew app that records video with the DSi's outer camera and turns those
recordings into animated GIFs on the console itself, no computer needed.

![Example video](resources/example1.gif)

## On the DSi

Put `dsi-camcorder.nds` on the SD card and run it. The bottom screen holds the menu,
the top screen shows what the camera (or the converter) is looking at.

- `A` records. The video is written to `sd:/DCIM/100DSI00/VID_####.BIN` and `START`
  stops and saves it.
- `B` converts a recording. The browser starts in `sd:/DCIM/100DSI00`, lists folders
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
