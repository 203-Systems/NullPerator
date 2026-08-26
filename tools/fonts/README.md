# Font assets

`font.svg` and the PNG files are source assets for PicoTracker fonts.

`import.py` converts a PNG character range into a C header. Run it without
arguments for the complete option list.

Examples:

```bash
python3 import.py font_hourglass.png --start 128 --end 255 --name SPECIAL_CHARS
python3 import.py font_wide.png --start 32 --end 127 --name FONT_WIDE
```
