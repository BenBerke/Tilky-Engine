-- Test only, never shipped: plays through the whole Classic FPS Kit.
-- Talks to the harness through an entity named "BOT:<keys>|<command>;<command>".

-- The routes below call these, so they're declared first.
local ignored, log, V, FxReport, Barrel, NearestEnemy, KickProbe

local ROUTES = {
    ["Main Menu"] = {
        { wait = 0.5 }, { shot = "menu" },
        -- Options: open, change Gore, back.
        { click = { 0.275, 0.705 } }, { wait = 0.2 }, { shot = "menu_options" },
        { click = { 0.275, 0.505 } }, { wait = 0.2 }, { shot = "menu_options_gore" },
        { call = function() log("menu gore now", V("GoreLevel")); Game.SetVariable("GoreLevel", 1) end },
        { click = { 0.275, 0.605 } }, { wait = 0.2 },
        { click = { 0.275, 0.605 } }, { wait = 0.2 }, { shot = "menu_levels" },
        { click = { 0.275, 0.505 } }, { wait = 1 },
    },
    ["Level 1 - Outpost"] = {
        { wait = 0.3 }, { shot = "l1_start" }, { report = "start" },
        -- The pause menu: open, screenshot, close with the button.
        { key = "Esc" }, { wait = 0.2 }, { shot = "l1_pause" }, { report = "paused" },
        { click = { 0.446, 0.365 } }, { wait = 0.2 }, { shot = "l1_resumed" }, { report = "resumed" },
        { go = { -24, 72 } }, { go = { 24, 72 } }, { report = "pickups" }, { shot = "l1_pickups" },
        { go = { 0, 88 } }, { face = { 0, 100 } }, { use = true }, { wait = 1.2 }, { shot = "l1_door" },
        { go = { 0, 130 } }, { go = { 0, 190 } }, { shot = "l1_hall" }, { wait = 0.1 }, { shot = "thumb" },
        { go = { 0, 200 } }, { report = "shotgun" }, { wait = 0.6 }, { shot = "l1_shotgun" },
        { go = { 0, 270 } }, { face = { 0, 300 } }, { use = true }, { wait = 2.5 }, { shot = "l1_lift_down" },
        { go = { 0, 304 } }, { wait = 5 }, { shot = "l1_lift_up" }, { report = "lift" },
        { go = { 0, 345 } }, { go = { 0, 370 } }, { face = { 0, 380 } }, { use = true }, { wait = 1.2 },
        { go = { 0, 395 } }, { go = { 0, 416 } }, { face = { 0, 432 } }, { shot = "l1_exit" }, { use = true }, { wait = 0.5 },
        { shot = "l1_intermission" }, { report = "intermission" },
        { wait = 0.6 }, { shot = "l1_tally" }, { click = { 0.5, 0.66 } }, { wait = 0.3 }, { shot = "l1_tally_done" }, { click = { 0.5, 0.66 } }, { wait = 1 },
    },
    ["Level 2 - Keys"] = {
        { wait = 0.3 }, { shot = "l2_start" }, { report = "start" },
        { go = { 0, 16 } }, { go = { 0, 40 } }, { report = "suit" },
        { go = { 0, 72 } }, { wait = 2.5 }, { shot = "l2_pool" }, { report = "pool" },
        { go = { 0, 110 } }, { face = { 0, 130 } }, { use = true }, { wait = 1 }, { shot = "l2_locked" },
        { go = { -88, 64 } }, { face = { -100, 64 } }, { use = true }, { wait = 1.2 },
        { go = { -120, 64 } }, { go = { -150, 64 } }, { go = { -190, 64 } }, { report = "red key" }, { shot = "l2_redkey" },
        { go = { -92, 64 } }, { go = { 0, 118 } }, { face = { 0, 130 } }, { use = true }, { wait = 1.2 },
        { go = { 0, 150 } }, { go = { -60, 180 } }, { face = { -72, 180 } }, { use = true }, { wait = 2.5 }, { shot = "l2_stairs" },
        { go = { 0, 205 } }, { go = { 0, 240 } }, { go = { 0, 300 } }, { report = "blue key" },
        { go = { 56, 275 }, teleports = true }, { wait = 0.5 }, { shot = "l2_teleported" }, { report = "teleported" },
        { go = { 0, 16 } }, { face = { 0, -4 } }, { use = true }, { wait = 1.2 }, { go = { 0, -24 } }, { report = "secret" }, { shot = "l2_secret" },
        { go = { 0, 16 } }, { go = { 88, 64 } }, { face = { 100, 64 } }, { use = true }, { wait = 1.2 },
        { go = { 120, 64 } }, { go = { 190, 64 } }, { face = { 204, 64 } }, { use = true }, { wait = 1.2 },
        { go = { 220, 64 } }, { go = { 240, 64 } }, { face = { 256, 64 } }, { shot = "l2_exit_switch" }, { use = true }, { wait = 0.5 },
        { report = "intermission" }, { shot = "l2_intermission" }, { click = { 0.5, 0.66 } }, { wait = 0.3 }, { click = { 0.5, 0.66 } }, { wait = 1 },
    },
    ["Level 3 - Courtyard"] = {
        { wait = 0.3 }, { shot = "l3_start" }, { report = "start" },
        -- Dying, and the death screen's Try Again. After it the level starts over.
        { die = true }, { wait = 2.5 }, { shot = "death" }, { report = "dead" },
        { click = { 0.476, 0.495 } }, { wait = 1 },
        { mark = "after_death" }, { wait = 0.5 }, { shot = "l3_after_death" }, { report = "pistol start" },
        { go = { 0, 94 } }, { face = { 0, 108 } }, { use = true }, { wait = 1.2 },
        { go = { 0, 125 } }, { shot = "l3_building" }, { go = { -30, 130 } }, { report = "minigun" },
        { go = { 0, 150 } }, { go = { 0, 190 } }, { report = "key" }, { wait = 1.5 }, { shot = "l3_trap" },
        { go = { 0, 125 } }, { go = { 0, 95 } }, { go = { -120, 95 } }, { go = { -130, 245 } }, { go = { -108, 248 } }, { face = { -108, 262 } }, { use = true }, { wait = 2.5 },
        { go = { -108, 268 } }, { wait = 5 }, { go = { -130, 300 } }, { report = "rockets" }, { shot = "l3_ledge" },
        { go = { -108, 268 } }, { wait = 3 }, { go = { -108, 248 } }, { go = { 130, 250 } }, { go = { 136, 314 } }, { face = { 136, 324 } }, { use = true }, { wait = 1.2 },
        { go = { 136, 340 } }, { go = { 136, 360 } }, { face = { 136, 376 } }, { shot = "l3_exit_switch" }, { use = true }, { wait = 0.5 },
        { report = "intermission" }, { shot = "l3_intermission" }, { click = { 0.5, 0.66 } }, { wait = 0.3 }, { click = { 0.5, 0.66 } }, { wait = 1 },
    },
    ["Level 4 - The Tripod"] = {
        { wait = 0.3 }, { shot = "l4_start" }, { report = "start" },
        { go = { -30, 60 } }, { go = { 30, 60 } }, { go = { 0, 60 } }, { report = "weapons" },
        { go = { 0, 76 } }, { face = { 0, 84 } }, { use = true }, { wait = 1.2 }, { go = { 0, 100 } }, { shot = "l4_arena" },
        { fight = 8 }, { shot = "l4_fight" },
        -- Only the boss still stands in the way: end it, so the gate test doesn't depend on aim.
        { killall = true }, { wait = 2 }, { report = "boss dead" }, { shot = "l4_gate" },
        { go = { 90, 200 } }, { go = { 90, 340 } }, { go = { 0, 365 } }, { go = { 0, 395 } }, { go = { 0, 416 } }, { face = { 0, 432 } }, { shot = "l4_exit_switch" }, { use = true }, { wait = 0.5 },
        { report = "victory" }, { shot = "l4_victory" }, { flag = "sawVictory" },
        { click = { 0.476, 0.895 } }, { wait = 1 },
    },
    ["Prefab Gallery"] = {
        { call = function() for _, e in ipairs(Game.FindEntitiesWithTag("Enemy")) do ignored[e.id] = true end end },
        { wait = 0.5 }, { shot = "gallery_a" }, { go = { 0, 100 } }, { shot = "gallery_b" },
        { call = function() FxReport("start") end },

        -- Wading in water.
        { go = { 0, 480 } }, { pitch = -55 }, { hold = "W", seconds = 0.45 }, { pitch = -55 }, { shot = "fx_water" }, { wait = 0.1 }, { shot = "fx_water2" }, { pitch = 0 }, { go = { 0, 470 } },

        -- A grenade: count down, hand on screen, flight, blast.
        { face = { 0, 400 } }, { key = "G" }, { wait = 0.1 }, { shot = "fx_throw_hand" }, { wait = 0.3 },
        { call = function() FxReport("thrown") end }, { shot = "fx_grenade_flying" }, { wait = 0.5 }, { shot = "fx_grenade_bounced" },
        { wait = 1.6 }, { shot = "fx_grenade_blast" }, { call = function() FxReport("blast") end }, { wait = 0.2 }, { shot = "fx_grenade_after" },
        { wait = 1.5 }, { shot = "fx_scorch" },

        -- A grenade into the pool.
        { face = { 0, 524 } }, { pitch = -8 }, { key = "G" }, { wait = 0.6 }, { shot = "fx_grenade_water" }, { wait = 2.6 }, { pitch = 0 },

        -- Kicking a barrel away.
        { go = function() local b = Barrel(); return { b.transform.position.x, 316 } end }, { wait = 0.5 },
        { face = function() local p = Barrel().transform.position; return { p.x, p.z } end },
        { call = function() local b = Barrel(); log("barrel before", b.transform.position.x, b.transform.position.z, "static", b.rigidbody.isStatic) end },
        { call = function() KickProbe() end }, { key = "F" }, { wait = 0.1 }, { shot = "fx_kick" }, { wait = 0.15 },
        { call = function() local b = Barrel(); log("barrel kicked", "static", b.rigidbody.isStatic, "speed", b.rigidbody.velocity.length, "y", b.transform.position.y) end },
        { wait = 0.8 },
        { call = function() local b = Barrel(); log("barrel after", b.transform.position.x, b.transform.position.y, b.transform.position.z, "vel", b.rigidbody.velocity.y, "grav", b.rigidbody.gravityScale, "floor", Game.GetSectorAt(b.transform.position):GetFloorHeightAt(b.transform.position), "mass", b.rigidbody.mass) end }, { shot = "fx_kick_after" },
        { go = function() local p = Barrel().transform.position; local e = entity.transform.position; local d = (e - p); local l = d.length; return { p.x + d.x / l * 24, p.z + d.z / l * 24 } end }, { wait = 0.6 },
        { face = function() local p = Barrel().transform.position; return { p.x, p.z } end }, { wait = 0.1 }, { shot = "fx_barrel_close" },
        { call = function() local b = Barrel(); log("barrel close", b.transform.position.y, "sprite", b.sprite.isActive, "scale", b.transform.scale.x, b.transform.scale.y, b.transform.scale.z, "eye", entity.camera:GetEyePosition().y) end },

        -- Shooting that barrel: blast, debris, burns.
        { call = function() Barrel().monsterAI.isActive = true; Global.ApplyDamage(Barrel(), 1000, entity) end }, { wait = 0.15 },
        { shot = "fx_barrel_blast" }, { call = function() FxReport("barrel"); for _, b in ipairs(Game.FindEntitiesWithTag("Kickable")) do log("barrel at", b.transform.position.x, b.transform.position.y, b.transform.position.z, b.name) end end }, { wait = 0.8 },

        -- Bullets into a monster, near the middle of its row.
        { go = { 20, 66 } }, { face = { 20, 40 } }, { call = function() Global.fxTarget = NearestEnemy(); Global.fxTarget.monsterAI.isActive = true; log("target", Global.fxTarget.name, "health", Global.fxTarget.monsterAI.health) end },
        { hold = "LCtrl", seconds = 0.7 }, { shot = "fx_blood" }, { call = function() FxReport("bullets") end }, { wait = 0.8 }, { shot = "fx_stains" },
        { call = function() local t = Global.fxTarget; log("target health", t.monsterAI.health, "state", t.monsterAI.state) end },

        -- Kicking a monster: damage and a shove.
        { call = function() local t = NearestEnemy(); t.monsterAI.isActive = true; Global.fxKicked = t; log("kicking", t.name, "health", t.monsterAI.health, "at", t.transform.position.x, t.transform.position.z, "body", t.rigidbody ~= nil) end },
        { aim = function() return Global.fxKicked end }, { go = function() local p = Global.fxKicked.transform.position; return { p.x, p.z + 14 } end }, { wait = 0.5 },
        { aim = function() return Global.fxKicked end }, { call = function() KickProbe() end }, { key = "F" }, { wait = 0.3 },
        { call = function() local t = Global.fxKicked; log("kicked", t.name, "health", t.monsterAI.health, "at", t.transform.position.x, t.transform.position.z) end },

        -- A plain kill leaves a pool. A huge hit bursts it.
        { call = function() local t = NearestEnemy(); t.monsterAI.isActive = true; Global.ApplyDamage(t, t.monsterAI.health, entity); log("killed", t.name) end },
        { wait = 1.5 }, { shot = "fx_pool" },
        { call = function() local t = NearestEnemy(); t.monsterAI.isActive = true; log("gibbing", t.name, t:HasTag("Bleeds")); Global.ApplyDamage(t, 100000, entity); Global.fxGib = t end },
        { wait = 0.2 }, { shot = "fx_gib" }, { call = function() FxReport("gib"); log("gib sprite shown", Global.fxGib.sprite.isActive) end }, { wait = 1.5 }, { shot = "fx_gib_after" },

        -- Blood on the screen, and every Gore level.
        { call = function() Global.ScreenBlood(60) end }, { wait = 0.1 }, { shot = "fx_screen_blood" }, { wait = 2.5 },
        { call = function() Game.SetVariable("GoreLevel", 0); log("gore off", Global.Fx.GoreOn()); Global.Fx.Blood(entity.transform.position + Vector3(0, 8, 0), Vector3(0, 0, 1)); Global.ScreenBlood(60) end },
        { wait = 0.05 }, { shot = "fx_gore_off" }, { call = function() FxReport("gore off") end },
        { call = function() Game.SetVariable("GoreLevel", 2); log("gore extra", Global.Fx.GoreOn()); Global.Fx.Blood(entity.transform.position + Vector3(0, 8, 8), Vector3(0, 0, 1)) end },
        { wait = 0.05 }, { shot = "fx_gore_extra" }, { call = function() FxReport("gore extra"); Game.SetVariable("GoreLevel", 1) end },

        -- Options, from the pause menu.
        { wait = 1 }, { key = "Esc" }, { wait = 0.2 }, { shot = "fx_pause" },
        { click = { 0.446, 0.565 } }, { wait = 0.2 }, { shot = "fx_options" },
        { click = { 0.446, 0.465 } }, { wait = 0.2 }, { shot = "fx_options_gore" }, { call = function() log("gore after click", V("GoreLevel"), "saved", Saving.HasKey("options", "gore")) end },
        { click = { 0.446, 0.565 } }, { wait = 0.2 }, { shot = "fx_options_back" }, { key = "Esc" }, { wait = 0.2 }, { shot = "fx_unpaused" },
        { call = function() Game.SetVariable("GoreLevel", 1); log("menu state", V("MenuState"), "time", GameTime.timeScale) end },

        -- How much a lot of particles costs.
        { wait = 1 },
        { call = function() local y = math.rad(entity.camera.yaw); local f = Vector3(math.sin(y), 0, math.cos(y)); for i = 1, 12 do Global.Fx.Blood(entity.transform.position + f * 25 + Vector3(i * 2 - 12, 30, 0), Vector3(0, -1, 0), 4) end end },
        { wait = 1 }, { wait = 0.1 }, { shot = "fx_stress" }, { wait = 1 }, { call = function() FxReport("end") end },
        { done = true },
    },
    ["Blank Base Level"] = { { wait = 0.5 }, { shot = "blank" }, { load = "Prefab Gallery" } },
}

local req
local route, step = nil, 1
local timer, stepTime = 0, 0
local mouseCommand = ""
local clickFrames = 0
local target, targetTime = nil, 0
local enemies = {}
local scan = 0
ignored = {}

function log(...)
    local parts = {}
    for i, v in ipairs({ ... }) do parts[i] = tostring(v) end
    Debug.LogWarning("BOT " .. table.concat(parts, " "))
end

function V(name) return Game.GetVariable(name) end

local function Report(label)
    log(Game.levelName, "[" .. label .. "] hp", V("Health"), "armor", V("Armor"), "bullets", V("Bullets"), "shells", V("Shells"),
        "rockets", V("Rockets"), "cells", V("Cells"), "kills", V("Kills") .. "/" .. V("TotalKills"), "items", V("Items") .. "/" .. V("TotalItems"),
        "secrets", V("Secrets") .. "/" .. V("TotalSecrets"), "keys", V("HasRedKey"), V("HasBlueKey"), V("HasYellowKey"), "ts", GameTime.timeScale, "menu", V("MenuState"))
end

local hasPlayer = false
local lastPos, blocked = nil, 0

local function Eye() return entity.camera:GetEyePosition() end

local function Center(e) return e.transform.position + Vector3(0, e.transform.scale.z * 0.5, 0) end

local function Alive(e)
    return e.isValid and e.monsterAI ~= nil and e.monsterAI.state ~= MonsterAIState.Dead
end

function FxReport(label)
    log("[fx " .. label .. "] grenades", V("Grenades"), "gore", V("GoreLevel"), "hp", V("Health"))
end

local fxBarrel = nil
function Barrel()
    if fxBarrel == nil or not fxBarrel.isValid then
        fxBarrel = Game.FindNearestWithTag("Kickable", Vector3(0, 0, 300))
    end
    return fxBarrel
end

function KickProbe()
    local eye, y, p = entity.camera:GetEyePosition(), math.rad(entity.camera.yaw), math.rad(entity.camera.pitch)
    local hit = Game.Raycast(eye, Vector3(math.sin(y) * math.cos(p), math.sin(p), math.cos(y) * math.cos(p)), 24, entity.id, true)
    log("kick probe from", eye.x, eye.y, eye.z, "yaw", entity.camera.yaw, "pitch", entity.camera.pitch, "hit", hit and hit.type, hit and hit.distance, hit and hit.entity and hit.entity.name)
end

function NearestEnemy()
    local best, bestDistance = nil, 1e9
    for _, e in ipairs(Game.FindEntitiesWithTag("Enemy")) do
        if Alive(e) and e:HasTag("Bleeds") then
            local d = (e.transform.position - entity.transform.position).length
            if d < bestDistance then best, bestDistance = e, d end
        end
    end
    return best
end

local function FindTarget()
    local best, bestDist = nil, 300
    local eye = Eye()
    for _, e in ipairs(enemies) do
        if Alive(e) and not ignored[e.id] then
            local d = mathT.Vector3Distance(eye, Center(e))
            if d < bestDist and Game.LineOfSight(eye, Center(e)) then best, bestDist = e, d end
        end
    end
    return best
end

function Start()
    req = Game.CreateEntity()
    -- Far away: the Use ray hits any entity's box, and this one would sit at the origin.
    req.transform.position = Vector3(0, -5000, 0)
    route = ROUTES[Game.levelName]
    -- Back at the menu after winning: finish with the two levels the route skips.
    if Game.levelName == "Main Menu" and Global.sawVictory then
        route = { { wait = 0.5 }, { shot = "menu_again" }, { load = "Blank Base Level" } }
    end
    hasPlayer = entity:HasTag("Player")
    enemies = Game.FindEntitiesWithTag("Enemy")
    log("start", Game.levelName, "enemies", #enemies, "route", route and #route or "none")
    -- After the death test the level restarts: carry on from there.
    if route and Global.deathTested then
        Global.dying = false
        for i, s in ipairs(route) do
            if s.mark == "after_death" then step = i end
        end
    end
end

function Update()
    local dt = GameTime.unscaledDeltaTime
    local keys, commands = {}, {}

    -- Keep the bot alive, except on purpose. One run has to cover the whole game.
    if hasPlayer and not Global.dying and V("Health") < 90 and V("Health") > 0 then
        log("healed at", V("Health"))
        Game.SetVariable("Health", 100)
    end

    -- The boss can kill in one hit. The bot is testing the level, not its aim.
    if hasPlayer and Game.levelName == "Level 4 - The Tripod" then Game.SetVariable("Invulnerable", 1) end

    -- A click is one frame down, one frame up.
    if clickFrames > 0 then
        commands[#commands + 1] = mouseCommand .. (clickFrames == 1 and ",1" or ",0")
        clickFrames = clickFrames - 1
    end

    if route == nil then
        req.name = "BOT:|" .. table.concat(commands, ";")
        return
    end

    -- Fight whatever is in sight first.
    local s = route[step]
    local busy = s ~= nil and (s.click ~= nil or s.shot ~= nil or s.key ~= nil or s.report ~= nil)
    if hasPlayer and GameTime.timeScale > 0 and not busy then
        scan = scan - dt
        if scan <= 0 then
            scan = 0.2
            enemies = Game.FindEntitiesWithTag("Enemy")
            if target == nil or not Alive(target) then target, targetTime = FindTarget(), 0 end
            -- A Pain Bringer spawns these for ever: fighting them all, the route would never go on.
            if target ~= nil and target.name == "Deadflare" then
                ignored[target.id] = true
                target = nil
            end
        end
        if target ~= nil and Alive(target) and Game.LineOfSight(Eye(), Center(target)) then
            targetTime = targetTime + dt
            if targetTime > 12 then
                log("gave up on", target.name, "health", target.monsterAI.health)
                ignored[target.id] = true
                target = nil
            else
                local d = Center(target) - Eye()
                entity.camera.yaw = math.deg(math.atan(d.x, d.z))
                entity.camera.pitch = math.deg(math.atan(d.y, math.sqrt(d.x * d.x + d.z * d.z)))
                keys[#keys + 1] = "LCtrl"
                req.name = "BOT:" .. table.concat(keys, ",") .. "|" .. table.concat(commands, ";")
                return
            end
        else
            target = nil
        end
    end

    if s == nil then
        req.name = "BOT:|" .. table.concat(commands, ";")
        return
    end
    stepTime = stepTime + dt

    local advance = false
    if type(s.go) == "function" then s.go = s.go() end
    if type(s.face) == "function" then s.face = s.face() end
    if s.mark then
        advance = true
    elseif s.call then
        s.call()
        advance = true
    elseif s.pitch then
        entity.camera.pitch = s.pitch
        advance = true
    elseif s.aim then
        local d = Center(s.aim()) - Eye()
        entity.camera.yaw = math.deg(math.atan(d.x, d.z))
        entity.camera.pitch = math.deg(math.atan(d.y, math.sqrt(d.x * d.x + d.z * d.z)))
        advance = true
    elseif s.hold then
        keys[#keys + 1] = s.hold
        timer = timer + dt
        if timer >= s.seconds then
            timer = 0
            advance = true
        end
    elseif s.flag then
        Global[s.flag] = true
        advance = true
    elseif s.die then
        Global.dying = true
        Global.deathTested = true
        Game.SetVariable("Health", 0)
        advance = true
    elseif s.shot then
        commands[#commands + 1] = "SHOT=" .. s.shot
        advance = true
    elseif s.report then
        Report(s.report)
        advance = true
    elseif s.go then
        local p = entity.transform.position
        local dx, dz = s.go[1] - p.x, s.go[2] - p.z
        if math.sqrt(dx * dx + dz * dz) < 4 then
            advance = true
        else
            entity.camera.yaw = math.deg(math.atan(dx, dz))
            entity.camera.pitch = 0
            keys[#keys + 1] = "W"
            -- Blocked: a door that closed behind us. A player would press Use.
            if lastPos ~= nil and math.abs(lastPos.x - p.x) + math.abs(lastPos.z - p.z) < 0.01 then
                blocked = blocked + dt
                if blocked > 0.5 then
                    blocked = 0
                    keys[#keys + 1] = "E"
                end
            else
                blocked = 0
            end
            lastPos = Vector3(p.x, p.y, p.z)
            if stepTime > 15 then
                log("STUCK going to", s.go[1], s.go[2], "at", p.x, p.y, p.z, "in", Game.levelName)
                Report("stuck")
                advance = true
            end
        end
    elseif s.face then
        local p = entity.transform.position
        entity.camera.yaw = math.deg(math.atan(s.face[1] - p.x, s.face[2] - p.z))
        entity.camera.pitch = 0
        advance = true
    elseif s.use then
        keys[#keys + 1] = "E"
        advance = true
    elseif s.key then
        keys[#keys + 1] = s.key
        advance = true
    elseif s.click then
        mouseCommand = "MOUSE=" .. s.click[1] .. "," .. s.click[2]
        clickFrames = 3
        commands[#commands + 1] = mouseCommand .. ",0"
        advance = true
    elseif s.wait then
        timer = timer + dt
        if timer >= s.wait then
            timer = 0
            advance = true
        end
    elseif s.fight then
        timer = timer + dt
        if timer >= s.fight then
            timer = 0
            advance = true
        end
    elseif s.killall then
        for _, e in ipairs(Game.FindEntitiesWithTag("Enemy")) do
            if Alive(e) then e.monsterAI:Hurt(entity, 100000) end
        end
        advance = true
    elseif s.load then
        Game.LoadLevel(s.load)
        advance = true
    elseif s.done then
        Report("done")
        commands[#commands + 1] = "DONE"
    end

    if advance then
        step = step + 1
        stepTime = 0
    end
    req.name = "BOT:" .. table.concat(keys, ",") .. "|" .. table.concat(commands, ";")
end
