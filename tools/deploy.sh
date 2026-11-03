#!/bin/sh
# Package the core and copy it onto the Pocket's SD card (Pocket in USB mode or card reader),
# together with the player's own game files the core reads (core/data.json): SCREAMER2.zip (the
# game's DATA, GFX and ANIMS folders in one stored ZIP, slot 3) and GOG's image of the CD,
# GAME.GOG (569 MB), with its cue sheet GAME.DAT (slots 22, 21), all at the top of the game
# folder, into Assets/screamer2/common/. A file already there with the same size is left as it is.
# Usage: tools/deploy.sh <SD card root> [game folder], e.g. tools/deploy.sh /i  (Git Bash for
#        drive I:); the game folder defaults to $GAME_DIR or "C:/GOG Games/Screamer 2"
# Only Screamer 2's own folders and files are written; everything else on the card stays as it is.
set -e
root=$(cd "$(dirname "$0")/.." && pwd)
card=${1:?usage: tools/deploy.sh <SD card root> [game folder]}
game=${2:-${GAME_DIR:-C:/GOG Games/Screamer 2}}
[ -d "$card/Cores" ] && [ -d "$card/Platforms" ] || { echo "$card does not look like a Pocket SD card"; exit 1; }
files="SCREAMER2.zip GAME.GOG GAME.DAT"
for f in $files; do
    [ -f "$game/$f" ] || { echo "$game/$f is missing: name the game's folder (argument or GAME_DIR)"; exit 1; }
done

sh "$root/tools/package.sh" > /dev/null
src="$root/build/sdcard"
cp -r "$src/." "$card/"
for f in $files; do
    dst="$card/Assets/screamer2/common/$f"
    mkdir -p "$(dirname "$dst")"
    [ -f "$dst" ] && [ "$(stat -c %s "$game/$f")" = "$(stat -c %s "$dst")" ] || cp "$game/$f" "$dst"
done
sync

cmp -s "$src/Cores/Chainy.Screamer2/bitstream.rbf_r" "$card/Cores/Chainy.Screamer2/bitstream.rbf_r"
echo "Deployed to $card:"
(cd "$src" && find . -type f | sed 's|^\./|  |' | sort)
for f in $files; do
    echo "  Assets/screamer2/common/$f  (from $game)"
done
