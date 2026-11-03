Screamer 2 for the Analogue Pocket - game files

The core needs the files of your own copy of Screamer 2 (1996). Copy these into this folder:

  SCREAMER2.zip  one ZIP of the game's folders DATA, GFX and ANIMS; how to make it is below
  GAME.GOG       the CD image with the game's music, from the game's install folder, as it is
  GAME.DAT       the cue sheet of the CD image, from the same folder, as it is

This folder is Assets/screamer2/common/ on the SD card. The core is tested with the GOG version.
When it starts, the core lists the files it found and the ones that are missing, and says what is
wrong with the ZIP. GAME.GOG and GAME.DAT only bring the music.

Make the ZIP with compression method "Store" (no compression): the core reads it fast.
Deflate, the usual ZIP compression, works too, but loading is slow; the core tells you when it is
used. Other methods (BZip2, LZMA, ...) are not supported: "unsupported compression, repack with Store".

How to make SCREAMER2.zip

  Windows 11 24H2 or later (File Explorer)
    1. Open the game's install folder and select the three folders DATA, GFX and ANIMS together.
    2. Right-click, "Compress to", "Additional options".
    3. In the "Create Archive" window: Archive format ZIP, Compression method Store, Archive
       name SCREAMER2.zip. Click Create.

  Other Windows versions: open a command prompt in the game's install folder and run
    tar --format zip --options zip:compression=store -cf SCREAMER2.zip DATA GFX ANIMS

  7-Zip: select the three folders, right-click, 7-Zip, "Add to archive...". Archive format zip,
    Compression level Store, archive name SCREAMER2.zip.

Put only DATA, GFX and ANIMS into the ZIP; the other files of the install folder are not needed.
