# 3dSNES Compatibility Matrix

Automated testing of US clean-dump ROMs. Each game was run for 8 seconds in 2D, then switched to 3D for 3 seconds. Screenshots captured automatically via `--test` mode.

**Rating scale:**
- **Great** — 3D rendering looks good, all major layers visible, playable
- **Good** — 3D works with minor artifacts (missing transparency, HDMA color issues)
- **Fair** — 3D partially works but has significant visual issues
- **Broken** — 3D view is unusable (solid color screen, garbled, Mode 7 not detected)
- **Boot only** — Only captured boot/logo screen (game needs longer to reach gameplay)

## Manually Tested (Gameplay Verified)

| Game | 2D | 3D | Rating | Notes |
|------|:---:|:---:|:---:|-------|
| Zelda: ALTTP | ![](docs/compat/Legend_of_Zelda,_The_-_A_Link_to_the_Past__U______2d.png) | ![](docs/compat/Legend_of_Zelda,_The_-_A_Link_to_the_Past__U______3d.png) | Great | Indoor rooms excellent. Top-down diorama. |
| Super Mario World | OK | OK | Great | All layers visible. Title, map, gameplay work. |
| Jurassic Park | ![](docs/jp_2d.png) | ![](docs/jp_3d.png) | Great | Top-down excellent. Mode 7 FPS auto-falls back to 2D. |
| Earthworm Jim | OK | OK | Good | Gameplay renders well. Color math artifacts. |
| TMNT IV: Turtles in Time | OK | ![](docs/tmnt_3d.png) | Good | Bridge fighting looks great. HDMA gradient issues. |
| Disney's Aladdin | OK | ![](docs/aladdin_3d.png) | Great | Agrabah marketplace is a standout. |
| Arkanoid: Doh It Again | ![](docs/compat/Arkanoid_-_Doh_It_Again__U______2d.png) | ![](docs/arkanoid_3d.png) | Great | Bricks as actual 3D blocks. |
| Caesars Palace | OK | ![](docs/caesars_3d.png) | Great | Building facade looks incredible. |
| Cool Spot | OK | ![](docs/coolspot_3d.png) | Good | Gameplay works well. |
| Super Street Fighter II | OK | ![](docs/sf2_3d.png) | Good | Character select works. HDMA intro garbled. |
| Mortal Kombat | OK | OK | Fair | Title works. HDMA background issues in fights. |
| Taz-Mania | OK | Broken | Broken | Rotation effects cause solid blue screen in 3D. |

## Automated Test Results

*Captured automatically at 8s boot. Many still on logo screens. Click to expand screenshots.*

| Game | 2D | 3D | Rating |
|------|:---:|:---:|:---:|
| 7th Saga | ![](docs/compat/7th_Saga,_The__U______2d.png) | ![](docs/compat/7th_Saga,_The__U______3d.png) | Boot only |
| AAAHH!!! Real Monsters | ![](docs/compat/AAAHH____Real_Monsters__U______2d.png) | ![](docs/compat/AAAHH____Real_Monsters__U______3d.png) | Boot only |
| AD&D: Eye of the Beholder | ![](docs/compat/AD_D_-_Eye_of_the_Beholder__U______2d.png) | ![](docs/compat/AD_D_-_Eye_of_the_Beholder__U______3d.png) | Boot only |
| ActRaiser | ![](docs/compat/ActRaiser__U______2d.png) | ![](docs/compat/ActRaiser__U______3d.png) | Boot only |
| ActRaiser 2 | ![](docs/compat/ActRaiser_2__U______2d.png) | ![](docs/compat/ActRaiser_2__U______3d.png) | Boot only |
| Addams Family | ![](docs/compat/Addams_Family,_The__U______2d.png) | ![](docs/compat/Addams_Family,_The__U______3d.png) | Broken |
| Addams Family Values | ![](docs/compat/Addams_Family_Values__U______2d.png) | ![](docs/compat/Addams_Family_Values__U______3d.png) | Good |
| Aladdin | ![](docs/compat/Aladdin__U______2d.png) | ![](docs/compat/Aladdin__U______3d.png) | Great |
| Axelay | ![](docs/compat/Axelay__U______2d.png) | ![](docs/compat/Axelay__U______3d.png) | Boot only |
| B.O.B. | ![](docs/compat/B.O.B.__U______2d.png) | ![](docs/compat/B.O.B.__U______3d.png) | Boot only |
| Boogerman | ![](docs/compat/Boogerman_-_A_Pick_and_Flick_Adventure__U______2d.png) | ![](docs/compat/Boogerman_-_A_Pick_and_Flick_Adventure__U______3d.png) | Good |
| Brain Lord | ![](docs/compat/Brain_Lord__U______2d.png) | ![](docs/compat/Brain_Lord__U______3d.png) | Boot only |
| Brandish | ![](docs/compat/Brandish__U______2d.png) | ![](docs/compat/Brandish__U______3d.png) | Boot only |
| Brawl Brothers | ![](docs/compat/Brawl_Brothers__U______2d.png) | ![](docs/compat/Brawl_Brothers__U______3d.png) | Boot only |
| Breath of Fire | ![](docs/compat/Breath_of_Fire__U______2d.png) | ![](docs/compat/Breath_of_Fire__U______3d.png) | Boot only |
| Breath of Fire II | ![](docs/compat/Breath_of_Fire_II__U______2d.png) | ![](docs/compat/Breath_of_Fire_II__U______3d.png) | Boot only |
| Bubsy | ![](docs/compat/Bubsy_in_Claws_Encounters_of_the_Furred_Kind__U______2d.png) | ![](docs/compat/Bubsy_in_Claws_Encounters_of_the_Furred_Kind__U______3d.png) | Boot only |
| Castlevania: Dracula X | ![](docs/compat/Castlevania_-_Dracula_X__U______2d.png) | ![](docs/compat/Castlevania_-_Dracula_X__U______3d.png) | Good |
| Chrono Trigger | ![](docs/compat/Chrono_Trigger__U______2d.png) | ![](docs/compat/Chrono_Trigger__U______3d.png) | Good |
| Frogger | ![](docs/compat/Frogger__U______2d.png) | ![](docs/compat/Frogger__U______3d.png) | Boot only |
| Gods | ![](docs/compat/Gods__U______2d.png) | ![](docs/compat/Gods__U______3d.png) | Boot only |
| Goof Troop | ![](docs/compat/Goof_Troop__U______2d.png) | ![](docs/compat/Goof_Troop__U______3d.png) | Boot only |
| Gradius III | ![](docs/compat/Gradius_III__U______2d.png) | ![](docs/compat/Gradius_III__U______3d.png) | Good |
| Illusion of Gaia | ![](docs/compat/Illusion_of_Gaia__U______2d.png) | ![](docs/compat/Illusion_of_Gaia__U______3d.png) | Good |
| Lemmings | ![](docs/compat/Lemmings__U___V1.1______2d.png) | ![](docs/compat/Lemmings__U___V1.1______3d.png) | Great |
| Lost Vikings | ![](docs/compat/Lost_Vikings,_The__U______2d.png) | ![](docs/compat/Lost_Vikings,_The__U______3d.png) | Boot only |
| Lost Vikings II | ![](docs/compat/Lost_Vikings_II,_The__U______2d.png) | ![](docs/compat/Lost_Vikings_II,_The__U______3d.png) | Boot only |
| Lufia | ![](docs/compat/Lufia___The_Fortress_of_Doom__U______2d.png) | ![](docs/compat/Lufia___The_Fortress_of_Doom__U______3d.png) | Boot only |
| Mega Man X | ![](docs/compat/Mega_Man_X__U___V1.0______2d.png) | ![](docs/compat/Mega_Man_X__U___V1.0______3d.png) | Boot only |
| Mega Man X2 | ![](docs/compat/Mega_Man_X_2__U______2d.png) | ![](docs/compat/Mega_Man_X_2__U______3d.png) | Boot only |
| Mega Man X3 | ![](docs/compat/Mega_Man_X_3__U______2d.png) | ![](docs/compat/Mega_Man_X_3__U______3d.png) | Boot only |
| Zelda: ALTTP | ![](docs/compat/Legend_of_Zelda,_The_-_A_Link_to_the_Past__U______2d.png) | ![](docs/compat/Legend_of_Zelda,_The_-_A_Link_to_the_Past__U______3d.png) | Great |

## Known Issues

| Issue | Affected Games | Description |
|-------|---------------|-------------|
| HDMA palette corruption | JP title, MK, TMNT intros | Per-scanline CGRAM changes cause garbled colors |
| Solid BG layer blocking | Addams Family, Taz-Mania | A full-screen BG layer covers the 3D view |
| Mode 7 not detected | Taz-Mania | Rotation/scaling effects that aren't PPU Mode 7 |
| Window masking missing | SMW text box, JP Dino DNA | Windowed BG regions render fully instead of clipped |
| Color math/transparency | EWJ, Cool Spot | Additive blending effects not extracted |
| Boot screen only | ~60% of automated tests | Test needs longer boot time (15-20s) for gameplay |

## Test Environment

- **Emulator:** LakeSnes (built-in)
- **Renderer:** CPU Software Rasterizer (256x224)
- **Platform:** Windows 11, RTX 5070
- **ROM source:** Clean US dumps `(U) [!]`
- **Test method:** `3dsnes --test` (8s 2D, 3s 3D, auto-exit)
