#!/usr/bin/env python3
"""Generate COMPATIBILITY.md + docs/gallery contact sheets from a --test corpus run.

Usage:  python make_compat.py [test_results_dir]

Reads the *_diag.json and *_3d_*.png / *_2d_*.png files that `test_all_roms.sh`
produces, picks the most representative 3D capture per game, packs them into
JPEG contact sheets, and writes a status table.

Status is derived, not hand-judged:
  no output    - 2D and 3D are both blank; the game does not boot (coprocessor)
  3D blank     - 2D has picture, 3D does not
  Mode 7       - PPU mode 7 at capture; 3D falls back to 2D by design
  renders      - 3D has picture
"""
import glob, json, math, os, sys
from PIL import Image, ImageStat

R = sys.argv[1] if len(sys.argv) > 1 else "test_results"
GALLERY = "docs/gallery"
PER_SHEET, COLS, CW, CH = 48, 4, 320, 240
BLANK = 4.0          # mean luma below this is "nothing on screen"


def luma(path):
    if not os.path.exists(path):
        return -1.0
    im = Image.open(path).convert("L").crop((0, 20, 1280, 960))
    return ImageStat.Stat(im).mean[0]


def pretty(basename):
    """Undo the filename sanitising well enough to read."""
    n = basename.replace("__U_____", "").replace("_", " ").strip()
    return " ".join(n.split())


def collect():
    games = []
    for j in sorted(glob.glob(f"{R}/*_diag.json")):
        try:
            d = json.load(open(j))
        except (json.JSONDecodeError, OSError):
            continue
        b = d["test_basename"]
        shots3 = [(luma(f"{R}/{b}_3d_0{i}.png"), f"{R}/{b}_3d_0{i}.png") for i in (3, 4, 5)]
        shots3 = [s for s in shots3 if s[0] >= 0]
        shots2 = [luma(f"{R}/{b}_2d_0{i}.png") for i in (1, 2, 6)]
        shots2 = [s for s in shots2 if s >= 0]
        if not shots3:
            continue
        l3, best = max(shots3)
        l2 = max(shots2) if shots2 else 0.0
        if l3 < BLANK and l2 < BLANK:
            status = "no output"
        elif l3 < BLANK:
            status = "3D blank"
        elif d["ppu_mode"] == 7:
            status = "Mode 7"
        else:
            status = "renders"
        games.append(dict(name=pretty(b), base=b, best=best, status=status,
                          mode=d["ppu_mode"], voxels=d["voxel_count"]))
    games.sort(key=lambda g: g["name"].lower())
    return games


def sheets(games):
    os.makedirs(GALLERY, exist_ok=True)
    for old in glob.glob(f"{GALLERY}/sheet*.jpg"):
        os.remove(old)
    for si in range(0, len(games), PER_SHEET):
        chunk = games[si:si + PER_SHEET]
        n = si // PER_SHEET + 1
        rows = math.ceil(len(chunk) / COLS)
        sheet = Image.new("RGB", (COLS * CW, rows * CH), (24, 24, 28))
        for i, g in enumerate(chunk):
            im = Image.open(g["best"]).convert("RGB").crop((0, 20, 1280, 960)).resize((CW, CH))
            sheet.paste(im, ((i % COLS) * CW, (i // COLS) * CH))
            g["sheet"] = n
        sheet.save(f"{GALLERY}/sheet{n:02d}.jpg", quality=86, optimize=True)


def write_md(games):
    tot = len(games)
    by = lambda s: [g for g in games if g["status"] == s]
    with open("COMPATIBILITY.md", "w", encoding="utf-8") as f:
        w = f.write
        w("# 3dSNES Compatibility\n\n")
        w(f"Every US clean-dump ROM in the test corpus (`*(U)*[!]*`), {tot} games, run "
          "unattended via `--test`: the harness boots each game, walks it past the "
          "attract screen with scripted input, switches to 3D and captures three "
          "frames plus a diagnostic JSON.\n\n")
        w("Status is measured from those captures, not hand-graded — it says whether "
          "the 3D view produced a picture, not whether the picture is beautiful. "
          "Browse the [gallery sheets](docs/gallery) to judge that yourself.\n\n")
        w("| Status | Games | Meaning |\n|---|---:|---|\n")
        for s, meaning in [
            ("renders", "3D view draws the scene"),
            ("Mode 7", "PPU Mode 7 at capture — 3D falls back to 2D by design"),
            ("3D blank", "game runs in 2D but the 3D view is empty"),
            ("no output", "game does not boot — unsupported coprocessor"),
        ]:
            n = len(by(s))
            w(f"| {s} | {n} ({100*n/tot:.0f}%) | {meaning} |\n")
        w("\n## Known gaps\n\n")
        for g in by("no output") + by("3D blank"):
            w(f"- **{g['name']}** — {g['status']}\n")
        w("\n## Gallery\n\n")
        nsheets = max(g.get("sheet", 1) for g in games)
        for n in range(1, nsheets + 1):
            part = [g for g in games if g.get("sheet") == n]
            w(f"### {part[0]['name']} — {part[-1]['name']}\n\n")
            w(f"![sheet {n}](docs/gallery/sheet{n:02d}.jpg)\n\n")
        w("## Full results\n\n| Game | Status | PPU mode | Voxels | Sheet |\n")
        w("|---|---|---:|---:|---:|\n")
        for g in games:
            w(f"| {g['name']} | {g['status']} | {g['mode']} | {g['voxels']} | {g.get('sheet','')} |\n")


if __name__ == "__main__":
    games = collect()
    assert games, f"no diagnostics found in {R}"
    sheets(games)
    write_md(games)
    counts = {}
    for g in games:
        counts[g["status"]] = counts.get(g["status"], 0) + 1
    print(len(games), "games:", counts)
