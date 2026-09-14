#!/usr/bin/env bash
# JiMStaff Milestone 10 — song-wide/per-staff sensory evidence.

set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/../.." && pwd)"
mscore="${1:-$root/build.m10.install/mscore.app/Contents/MacOS/mscore}"

[ -x "$mscore" ] || { echo >&2 "no mscore bundle at $mscore"; exit 1; }

renders="$here/renders"
rm -rf "$renders"
mkdir -p "$renders"

declare -a NAMES=(empty-template old-hundredth)
declare -a SOURCES=(
    "$root/share/templates/02-Choral/12-SATB_(MeloPresto_Staff)/12-SATB_(MeloPresto_Staff).mscx"
    "$root/src/engraving/tests/jimstaff_data/m9-satb-hymn.mscx"
)

scratch_root="$(mktemp -d)"
trap 'rm -rf "$scratch_root"' EXIT

for i in "${!NAMES[@]}"; do
    name="${NAMES[$i]}"
    src="${SOURCES[$i]}"
    [ -f "$src" ] || { echo >&2 "missing fixture: $src"; exit 1; }
    for run in 1 2; do
        d="$scratch_root/$name-$run"
        mkdir -p "$d"
        cp "$src" "$d/score.mscx"
        (cd "$d" && QT_QPA_PLATFORM=offscreen "$mscore" -r 120 -o page.png score.mscx >/dev/null 2>&1)
        for f in "$d"/page-*.png; do
            k="$(basename "$f" .png)"
            k="${k#page-}"
            if [ "$run" = 1 ]; then
                cp "$f" "$renders/$name-p$k.png"
            else
                cp "$f" "$renders/$name-2-p$k.png"
            fi
        done
    done
done

(cd "$renders" && shasum -a 256 ./*.png | sed 's#\./##' > SHA256SUMS)
echo "wrote $(ls "$renders"/*.png | wc -l | tr -d ' ') renders and SHA256SUMS in $renders"
