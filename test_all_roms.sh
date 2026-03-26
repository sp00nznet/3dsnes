#!/bin/bash
set +H  # disable history expansion (! in filenames)

# test_all_roms.sh — Sequential ROM testing. No background processes.
# Each game runs ~45 seconds in foreground, takes 6 screenshots + JSON diagnostic.
# Total time: ~375 games * 45s = ~4.5 hours

ROMDIR="Z:/Roms/SNES"
EMUDIR="E:/3dsnes/build/Release"
OUTDIR="E:/3dsnes/test_results"
MANIFEST="$OUTDIR/manifest.txt"

mkdir -p "$OUTDIR"
: > "$MANIFEST"

echo "=== 3dSNES ROM Test Suite v2 ==="
echo "Started: $(date)"
echo "Output: $OUTDIR"

# Build ROM list
find "$ROMDIR" -type f -iname "*(U)*\[!\]*.zip" | sort > "$OUTDIR/romlist.txt"
total=$(wc -l < "$OUTDIR/romlist.txt")
echo "Found $total ROMs (est. $(( total * 45 / 60 )) minutes)"
echo ""

rom_count=0

while IFS= read -r rompath; do
    romname=$(basename "$rompath")
    rom_count=$((rom_count + 1))

    printf "[%03d/%03d] %-55s " "$rom_count" "$total" "$romname"

    # Run in FOREGROUND — blocks until test completes or crashes
    "$EMUDIR/3dsnes.exe" "$rompath" --test < /dev/null > /dev/null 2>&1
    exit_code=$?

    if [ $exit_code -ne 0 ]; then
        echo "FAIL($exit_code)"
        echo "$rom_count|$romname|FAIL|$exit_code" >> "$MANIFEST"
    else
        echo "OK"
        echo "$rom_count|$romname|OK" >> "$MANIFEST"
    fi

done < "$OUTDIR/romlist.txt"

# Move all test output to results dir
mv "$EMUDIR"/*_2d_*.png "$OUTDIR/" 2>/dev/null
mv "$EMUDIR"/*_3d_*.png "$OUTDIR/" 2>/dev/null
mv "$EMUDIR"/*_diag.json "$OUTDIR/" 2>/dev/null

echo ""
echo "=== Complete ==="
echo "Finished: $(date)"
echo "Results in: $OUTDIR"
echo "Screenshots: $(ls "$OUTDIR"/*.png 2>/dev/null | wc -l)"
echo "Diagnostics: $(ls "$OUTDIR"/*.json 2>/dev/null | wc -l)"
