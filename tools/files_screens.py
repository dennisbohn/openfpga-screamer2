"""The archives for the pictures of the files screen (tools/files_screens.sh), made of one byte per
file under the real ZIP's file names (no game data), in the folder given:

    python -I tools/files_screens.py <real.man of tools/check_zip.py> <out folder>

  missing.zip    the real file list without GFX/MAP3H01.DAT (a race file)
  nonight.zip    without the optional files of the night set and the reflections
  deflated.zip   the same list, deflated (loads slowly: the notice)
  unsupported.zip  the same list, BZip2
  topfolder.zip  every name behind "Screamer 2/": found just the same
  foreign.zip    the game's other folders, not DATA, GFX, ANIMS
  notzip.zip     bytes that are not a ZIP
"""
import os
import random
import sys
import zipfile

names = [l.split("\t")[1] for l in open(sys.argv[1]).read().splitlines()]
out = sys.argv[2]
os.makedirs(out, exist_ok=True)


def zip_of(fn, files, method=zipfile.ZIP_STORED):
    with zipfile.ZipFile(os.path.join(out, fn), "w", method) as z:
        for n in files:
            z.writestr(n, b"x" * 100)


zip_of("missing.zip", [n for n in names if n != "GFX/MAP3H01.DAT"])
zip_of("nonight.zip", [n for n in names if not any(k in n for k in ("H11", "11.DAT", "ENVP", "ENVM"))])
zip_of("deflated.zip", names, zipfile.ZIP_DEFLATED)
zip_of("unsupported.zip", names, zipfile.ZIP_BZIP2)
zip_of("topfolder.zip", ["Screamer 2/" + n for n in names])
zip_of("foreign.zip", ["CONFIG/CHOICE.DAT", "TABS/VIS.TAB", "S2.BAT"])
open(os.path.join(out, "notzip.zip"), "wb").write(random.Random(5).randbytes(100000))
