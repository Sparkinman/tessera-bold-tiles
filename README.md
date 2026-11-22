# Bold tiles

Tiles that show an entity in large bold letters for a screen you read from across the room: the name small at the top,
the thing that matters large under it. Tessera's own fonts stop at a fixed size, so this plugin brings bold Roboto (the
screen's own typeface) in five sizes, 32 to 108 px, and each tile takes the largest that fits. It costs about 500 KB of
flash, so it is meant for the larger boards.

- **Bold value**: a sensor's number, large and bold, its unit smaller beside it (Orem 33.3 °F). Decimals automatic,
  none, one or two.
- **Bold state**: an entity's state as one large word: Open or Closed for doors, windows and gates, Locked or Unlocked,
  On or Off. Red while something stands open (can be turned off). With **Tap: Popup to lock / unlock** a tap opens a large popup with
  the state and one big key to lock or unlock (or open or close a cover, or turn a switch on or off).
- **Bold switch**: a light or switch with its name large and bold, over two lines if need be, On or Off under it. A tap
  turns it on or off; a soft colour shows behind it while it is on.

Every tile has a **Label** (the words shown; changing it updates the screen at once, unlike the tile name, which
Tessera only sends when the tile is made) and a **Text size** (Normal, Large, Huge): how much of the tile the large text may take.

## Which screens

Made for the **larger touchscreens** (7" and 10" ESP32-P4 boards such as the Guition JC1060P470 and JC8012P4A1, which
have 16 MB of flash and plenty of PSRAM). Every size is drawn at full resolution, so the text stays smooth. Tessera's own fonts stop at 64 px and drawing them larger only blurs
them, so this plugin brings real large sizes of Roboto (the screen's own typeface). That costs about 500 KB of flash,
which the 4 MB boards (most 4" screens) do not have to spare. Install it only on screens that can carry the extra
overhead.

## Install for testing

Copy this folder to `/config/tessera-plugins/bold_tiles/` on Home Assistant. It then shows on Tessera's Plugins page
labelled Test. Add it to a screen and place the tiles from the library's Plugins group.
