#!/bin/bash
# analyze_results.sh — Scan test results and identify patterns
# Run after test_all_roms.sh completes.

OUTDIR="E:/3dsnes/test_results"

if [ ! -d "$OUTDIR" ]; then
    echo "No results directory found. Run test_all_roms.sh first."
    exit 1
fi

total_json=$(ls "$OUTDIR"/*.json 2>/dev/null | wc -l)
echo "=== 3dSNES Test Analysis ==="
echo "Analyzing $total_json game diagnostics"
echo ""

# --- Group by PPU mode ---
echo "## PPU Mode Distribution"
for mode in 0 1 2 3 4 5 6 7; do
    count=$(grep -l "\"ppu_mode\": $mode" "$OUTDIR"/*.json 2>/dev/null | wc -l)
    if [ "$count" -gt 0 ]; then
        echo "  Mode $mode: $count games"
        if [ "$count" -le 10 ]; then
            grep -l "\"ppu_mode\": $mode" "$OUTDIR"/*.json 2>/dev/null | while read f; do
                name=$(grep '"rom"' "$f" | sed 's/.*: "//;s/".*//' | sed 's/.*\///')
                echo "    - $name"
            done
        fi
    fi
done
echo ""

# --- Games with 0 voxels (broken extraction) ---
echo "## Zero Voxels (Broken 3D Extraction)"
grep -l '"voxel_count": 0' "$OUTDIR"/*.json 2>/dev/null | while read f; do
    name=$(grep '"rom"' "$f" | sed 's/.*: "//;s/".*//' | sed 's/.*\///')
    mode=$(grep '"ppu_mode"' "$f" | sed 's/.*: //;s/,//')
    echo "  - $name (Mode $mode)"
done
echo ""

# --- Games with Mode 7 detected ---
echo "## Mode 7 Detected (Auto-Fallback to 2D)"
grep -l '"mode7_detected": true' "$OUTDIR"/*.json 2>/dev/null | while read f; do
    name=$(grep '"rom"' "$f" | sed 's/.*: "//;s/".*//' | sed 's/.*\///')
    echo "  - $name"
done
echo ""

# --- Games with high voxel count (performance concerns) ---
echo "## High Voxel Count (>200K, Performance Risk)"
for f in "$OUTDIR"/*.json; do
    count=$(grep '"voxel_count"' "$f" 2>/dev/null | sed 's/.*: //;s/,//')
    if [ -n "$count" ] && [ "$count" -gt 200000 ] 2>/dev/null; then
        name=$(grep '"rom"' "$f" | sed 's/.*: "//;s/".*//' | sed 's/.*\///')
        echo "  - $name (${count} voxels)"
    fi
done
echo ""

# --- Games with brightness 0 (still fading in at test time) ---
echo "## Brightness 0 at Test End (Game May Not Have Finished Booting)"
grep -l '"brightness": 0' "$OUTDIR"/*.json 2>/dev/null | while read f; do
    name=$(grep '"rom"' "$f" | sed 's/.*: "//;s/".*//' | sed 's/.*\///')
    echo "  - $name"
done
echo ""

# --- Games with no sprites (unusual) ---
echo "## No Sprites Enabled"
grep -l '"enabled": false' "$OUTDIR"/*.json 2>/dev/null | grep -l '"count": 0' 2>/dev/null | while read f; do
    name=$(grep '"rom"' "$f" | sed 's/.*: "//;s/".*//' | sed 's/.*\///')
    echo "  - $name"
done
echo ""

# --- Summary ---
echo "## Summary"
echo "  Total games tested: $total_json"
echo "  Screenshots: $(ls "$OUTDIR"/*.png 2>/dev/null | wc -l)"
echo "  2D screenshots: $(ls "$OUTDIR"/*_2d_*.png 2>/dev/null | wc -l)"
echo "  3D screenshots: $(ls "$OUTDIR"/*_3d_*.png 2>/dev/null | wc -l)"
echo ""
echo "=== End Analysis ==="
