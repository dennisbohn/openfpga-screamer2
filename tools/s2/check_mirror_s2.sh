#!/bin/sh
# Host test of the rear-view mirror's pass (firmware/track.c track_draw_mirror, docs/screamer2/hud.md "The views from the car"): the scene drawn from a camera into the top 35 rows by the
# ordinary pass and by the mirror's must be each other's mirror image (the pixels of the mirror's row y at x are the ordinary pass's at 319 - x: the picture turned over about its middle, as the game does it),
# on all seven tracks from a camera at the road looking back along it; with every block at level 0 (NOLOD=1: the mirror's pass takes level 1 at least, which is not what the flip is about). The
# negative control draws the mirror's pass with the ordinary pass's own sense (tools/s2/render_s2.c's pass 1) and the flip must fail. Needs build/s2/track<n>.bin (tools/s2/make_track_s2.py).
# Usage: tools/s2/check_mirror_s2.sh [tracks...]      default all seven; about 2 minutes
set -e
root=$(cd "$(dirname "$0")/../.." && pwd)
tracks=${*:-1 2 3 4 5 6 7}
mkdir -p "$root/build/check/mirror"
for n in $tracks; do
    img="$root/build/s2/track$n.bin"
    [ -f "$img" ] || { echo "no $img (python -I tools/s2/make_track_s2.py <game folder> $n $img)"; exit 1; }
    # cameras over the road at points of the map (r<i>, looking along it)
    cams=""
    for i in 30 150 300 450 600 750; do cams="$cams r$i"; done
    NOLOD=1 MIRROR_ROWS=35 sh "$root/tools/s2/render_s2.sh" "$img" "$root/build/check/mirror/t$n" $cams > "$root/build/check/mirror/t$n.txt" 2>&1 || { tail -5 "$root/build/check/mirror/t$n.txt"; exit 1; }
done
python -I "$root/tools/s2/check_mirror_s2.py" "$root/build/check/mirror" $tracks
