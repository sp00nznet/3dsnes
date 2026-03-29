--
-- Super Mario World — 3dSNES Lua script
--
-- Reads SNES RAM to detect game state and adjusts the 3D scene accordingly.
--
-- SMW RAM map (key addresses):
--   $0071 = Player animation state (0=walking, 1=climbing, etc.)
--   $0013 = Player screen X position
--   $0100 = Game mode (0x14 = normal gameplay)
--   $13D4 = Pause flag (nonzero = paused)
--   $0DBF = Number of exits found
--   $1493 = Star power timer
--   $0DDA = Coins
--   $0F34 = Yoshi flag
--

local frame_num = 0
local last_game_mode = 0

function Start()
    log("Super Mario World script loaded!")
    log("  Game mode addr: $0100")
    log("  Pause addr: $13D4")
end

function Update()
    frame_num = frame.number()
    local game_mode = snes.read(0x0100)
    local paused = snes.read(0x13D4)
    local star_timer = snes.read16(0x1493)

    -- Fade backgrounds when paused
    if paused ~= 0 then
        profile.setLayerAlpha(0, 0.4)
        profile.setLayerAlpha(1, 0.3)
        profile.setSpriteAlpha(0.5)
    else
        profile.setLayerAlpha(0, 1.0)
        profile.setLayerAlpha(1, 1.0)
        profile.setSpriteAlpha(1.0)
    end

    -- Star power: pulsing bright lighting
    if star_timer > 0 then
        local pulse = 0.5 + 0.5 * math.sin(frame_num * 0.3)
        profile.setAmbient(0.35 + pulse * 0.4)
        profile.setDiffuse(0.65 + pulse * 0.2)
    else
        profile.setAmbient(0.35)
        profile.setDiffuse(0.65)
    end

    -- Hide status bar (BG layer 2, top 28 pixels)
    for i = 0, tile.count() - 1 do
        local info = tile.getInfo(i)
        if info.layer == 2 and info.screen_y < 28 then
            tile.setHidden(i, true)
        end
    end

    -- Log game mode changes
    if game_mode ~= last_game_mode then
        log("Game mode changed: " .. string.format("0x%02X", game_mode))
        last_game_mode = game_mode
    end
end
