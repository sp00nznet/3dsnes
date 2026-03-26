#!/bin/bash
set +H  # disable history expansion (! in filenames like "AAAHH!!!")
# test_all_roms.sh — Automated ROM testing with --test mode

ROMDIR="Z:/Roms/SNES"
EMUDIR="E:/3dsnes/build/Release"
MANIFEST="$EMUDIR/screenshot_manifest.txt"
ROMLIST="$EMUDIR/romlist.txt"

rm -f "$EMUDIR"/screenshot_*.png
: > "$MANIFEST"

echo "=== 3dSNES ROM Test Suite ==="
echo "Started: $(date)"

# Build ROM list to file first (avoids pipe stdin issues)
find "$ROMDIR" -type f -iname "*(U)*\[!\]*.zip" | sort > "$ROMLIST"
total=$(wc -l < "$ROMLIST")
echo "Found $total ROMs"

rom_count=0

while IFS= read -r rompath; do
    romname=$(basename "$rompath")
    rom_count=$((rom_count + 1))

    printf "[%03d/%03d] %-50s " "$rom_count" "$total" "$romname"

    # Launch with stdin from /dev/null so it doesn't inherit the pipe
    "$EMUDIR/3dsnes.exe" "$rompath" --test < /dev/null > /dev/null 2>&1 &
    PID=$!

    # Poll for up to 18 seconds
    DEAD=0
    for i in $(seq 1 18); do
        if ! kill -0 $PID 2>/dev/null; then
            DEAD=1
            break
        fi
        sleep 1
    done

    if [ $DEAD -eq 0 ]; then
        taskkill //F //PID $PID > /dev/null 2>&1
        wait $PID 2>/dev/null
        echo "TIMEOUT"
        echo "$rom_count|$romname|TIMEOUT" >> "$MANIFEST"
    else
        wait $PID 2>/dev/null
        echo "OK"
        echo "$rom_count|$romname|OK" >> "$MANIFEST"
    fi
done < "$ROMLIST"

echo ""
echo "=== Complete ==="
echo "Finished: $(date)"
echo "Screenshots: $(ls "$EMUDIR"/screenshot_*.png 2>/dev/null | wc -l)"
rm -f "$ROMLIST"
