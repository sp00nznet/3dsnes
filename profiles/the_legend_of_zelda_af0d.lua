--
-- The Legend of Zelda: A Link to the Past — 3dSNES Lua script
--
-- Zelda ALTTP RAM map (key addresses):
--   $0010 = Game state (0x07 = dungeon, 0x09 = overworld, 0x0E = text/menu)
--   $0011 = Sub-game state
--   $001B = Current layer (0 = light world, 1 = dark world)
--   $0372 = Link's Y position on screen
--   $0022 = Link's direction facing
--   $0046 = Link's invincibility timer
--   $040C = Room ID (dungeon)
--   $008A = Overworld area ID
--

local frame_num = 0

function Start()
    log("Zelda: ALTTP script loaded!")
end

function Update()
    frame_num = frame.number()
    local game_state = snes.read(0x0010)
    local sub_state = snes.read(0x0011)
    local link_invincible = snes.read(0x0046)

    -- When Link is hit (invincibility timer active), flash sprites
    if link_invincible > 0 then
        local flash = math.abs(math.sin(frame_num * 0.5))
        profile.setSpriteAlpha(0.5 + 0.5 * flash)
    else
        profile.setSpriteAlpha(1.0)
    end

    -- Text/menu screens: flatten the scene
    if game_state == 0x0E or game_state == 0x0F then
        profile.setLayerAlpha(0, 0.3)
        profile.setLayerAlpha(1, 0.3)
    else
        profile.setLayerAlpha(0, 1.0)
        profile.setLayerAlpha(1, 1.0)
    end

    -- Dark World: shift lighting to feel darker
    local world = snes.read(0x001B)
    if world == 1 then
        profile.setAmbient(0.25)
        profile.setLightDir(0.2, 0.6, 0.4)
    else
        profile.setAmbient(0.35)
        profile.setLightDir(0.3, 0.8, 0.5)
    end
end
