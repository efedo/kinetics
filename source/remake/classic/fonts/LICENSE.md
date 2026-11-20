# Font data

`vga9x16.inc` (IBM VGA 9×16 text-mode font) and `bios8x8.inc` (IBM PC BIOS
8×8 font, as used by Borland's BGI default font) are generated from
**The Ultimate Oldschool PC Font Pack** v2.2 by **VileR**
(<https://int10h.org/oldschool-pc-fonts/>), files
`Bm437_IBM_VGA_9x16.FON` and `Bm437_IBM_BIOS.FON`.

They are licensed under the
[Creative Commons Attribution-ShareAlike 4.0 International License](https://creativecommons.org/licenses/by-sa/4.0/)
(CC BY-SA 4.0), unlike the rest of this repository. The only change is the
conversion from Windows `.FON` resources to C arrays of glyph rows.
