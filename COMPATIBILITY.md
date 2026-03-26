# 3dSNES Compatibility Matrix

Automated testing of 156 US clean-dump ROMs. Each game was run for 8 seconds in 2D, then switched to 3D for 3 seconds. Screenshots captured automatically.

**Rating scale:**
- **Great** — 3D rendering looks good, all major layers visible, playable
- **Good** — 3D works with minor artifacts (missing transparency, HDMA color issues)
- **Fair** — 3D partially works but has significant visual issues
- **Broken** — 3D view is unusable (solid color screen, garbled, Mode 7 not detected)
- **Boot only** — Only captured boot/logo screen (game needs longer to reach gameplay)

## Manually Tested (Gameplay Verified)

| Game | 2D | 3D | Rating | Notes |
|------|:---:|:---:|:---:|-------|
| Zelda: A Link to the Past | OK | OK | Great | Indoor rooms excellent. Top-down diorama. |
| Super Mario World | OK | OK | Great | All layers visible. Title screen, map, gameplay work. |
| Jurassic Park | OK | OK | Great | Top-down sections excellent. Mode 7 FPS auto-falls back to 2D. |
| Earthworm Jim | OK | OK | Good | Gameplay renders well. Some color math transparency artifacts. |
| TMNT IV: Turtles in Time | OK | OK | Good | Bridge/street fighting looks great. HDMA gradient issues on intros. |
| Disney's Aladdin | OK | OK | Great | Agrabah marketplace is a standout scene. |
| Arkanoid: Doh It Again | OK | OK | Great | Perfect fit — bricks render as actual 3D blocks. |
| Caesars Palace | OK | OK | Great | Building facade and signs look incredible as voxel dioramas. |
| Cool Spot | OK | OK | Good | Gameplay works well. Fun art style. |
| Super Street Fighter II | OK | OK | Good | Character select and stages work. HDMA intro effects garbled. |
| Mortal Kombat | OK | OK | Fair | Title works. Fights have HDMA background issues. |
| Taz-Mania | OK | Broken | Broken | Uses rotation effects. Solid blue screen in 3D. |

## Automated Test Results (Boot Screen Captures)

*These were captured automatically at 8 seconds — many games are still on logo/copyright screens. Games marked "Boot only" need manual testing to verify gameplay.*

| Game | 2D | 3D | Rating | Notes |
|------|:---:|:---:|:---:|-------|
| 7th Saga | OK | OK | Boot only | Enix logo |
| AAAHH!!! Real Monsters | OK | OK | Boot only | |
| AD&D: Eye of the Beholder | OK | OK | Boot only | |
| ActRaiser | OK | OK | Boot only | Enix logo with depth |
| ActRaiser 2 | OK | OK | Boot only | |
| Addams Family | OK | Broken | Broken | Solid blue BG layer covers 3D view |
| Addams Family Values | OK | OK | Good | Ocean logo renders with nice depth |
| Adventures of Kid Kleets | OK | OK | Boot only | |
| Aero the Acro-Bat | OK | OK | Boot only | |
| Aerobiz | OK | OK | Boot only | |
| Aladdin | OK | OK | Great | Title screen with depth |
| Alien 3 | OK | OK | Boot only | |
| Andre Agassi Tennis | OK | OK | Boot only | |
| Animaniacs | OK | OK | Boot only | |
| Arkanoid: Doh It Again | OK | OK | Great | Title and gameplay |
| Axelay | OK | OK | Boot only | |
| B.O.B. | OK | OK | Boot only | |
| Ballz 3D | OK | OK | Boot only | |
| Batman Forever | OK | OK | Boot only | |
| Blues Brothers | OK | OK | Boot only | |
| Boogerman | OK | OK | Good | Title screen with slimy green depth |
| Brain Lord | OK | OK | Boot only | Title text |
| Brandish | OK | OK | Boot only | |
| Brawl Brothers | OK | OK | Boot only | |
| Breath of Fire | OK | OK | Boot only | Capcom/Squaresoft copyright |
| Breath of Fire II | OK | OK | Boot only | |
| Brett Hull Hockey | OK | OK | Boot only | |
| Bubsy | OK | OK | Boot only | |
| Bugs Bunny: Rabbit Rampage | OK | OK | Boot only | |
| Cal Ripken Jr. Baseball | OK | OK | Boot only | |
| Cannondale Cup | OK | OK | Boot only | |
| Castlevania: Dracula X | OK | OK | Boot only | Konami logo with metallic gradient depth |
| Chester Cheetah | OK | OK | Boot only | |
| Chrono Trigger | OK | OK | Good | Golden pendulum intro — beautiful sprite work |
| Civilization | OK | OK | Boot only | |
| Clay Fighter | OK | OK | Boot only | |
| Clay Fighter 2 | OK | OK | Boot only | |
| Claymates | OK | OK | Boot only | |
| Clue | OK | OK | Boot only | |
| Frogger | OK | OK | Boot only | |
| Gods | OK | OK | Boot only | |
| Goof Troop | OK | OK | Boot only | Capcom logo |
| Gradius III | OK | OK | Good | Konami logo with gorgeous metallic gradient |
| Illusion of Gaia | OK | OK | Good | Earth globe intro — stunning sprite rendering |
| Indiana Jones Greatest Adventures | OK | OK | Boot only | |
| Lemmings | OK | OK | Great | Rocky cliff gameplay with lemming — excellent 3D depth |
| Lethal Weapon | OK | OK | Boot only | |
| Lost Vikings | OK | OK | Boot only | |
| Lost Vikings II | OK | OK | Boot only | |
| Lufia & The Fortress of Doom | OK | OK | Boot only | |
| Madden NFL '94-'97 | OK | OK | Boot only | |
| Magic Boy | OK | OK | Boot only | |
| Magical Quest: Mickey Mouse | OK | OK | Boot only | |
| Mega Man X | OK | OK | Boot only | SNK/Capcom boot text |
| Mega Man X2 | OK | OK | Boot only | |
| Mega Man X3 | OK | OK | Boot only | |
| Mega Man's Soccer | OK | OK | Boot only | |
| Michael Jordan: Chaos in the Windy City | OK | OK | Boot only | |
| Mickey Mania | OK | OK | Boot only | |
| Zelda: A Link to the Past | OK | OK | Great | Triforce intro with depth |

## Known Issues

| Issue | Affected Games | Description |
|-------|---------------|-------------|
| HDMA palette corruption | JP title, MK, TMNT intros | Per-scanline CGRAM changes cause garbled colors |
| Solid BG layer blocking | Addams Family, Taz-Mania | A full-screen BG layer covers the 3D view |
| Mode 7 not detected | Taz-Mania | Rotation/scaling effects that aren't PPU Mode 7 |
| Window masking missing | SMW text box, JP Dino DNA | Windowed BG regions render fully instead of clipped |
| Color math/transparency | EWJ, Cool Spot | Additive blending effects not extracted |
| Boot screen only | ~60% of tested games | Automated test needs longer boot time (15-20s) |

## Test Environment

- **Emulator:** LakeSnes (built-in)
- **Renderer:** CPU Software Rasterizer (256x224)
- **Platform:** Windows 11, RTX 5070
- **ROM source:** Clean US dumps `(U) [!]` from standard ROM sets
- **Test method:** `3dsnes --test` flag (8s 2D screenshot, 3s 3D screenshot, auto-exit)
