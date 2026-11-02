-- Test only, never shipped: plays through the whole Classic FPS Kit.
-- Talks to the harness through an entity named "BOT:<keys>|<command>;<command>".

local ROUTES = {
    ["Main Menu"] = {
        { wait = 0.5 }, { shot = "menu" },
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
        { go = { 0, 395 } }, { go = { 0, 412 } }, { face = { 0, 424 } }, { shot = "l1_exit" }, { use = true }, { wait = 0.5 },
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
        { go = { 0, 150 } }, { go = { -48, 176 } }, { face = { -60, 180 } }, { use = true }, { wait = 2.5 }, { shot = "l2_stairs" },
        { go = { 0, 205 } }, { go = { 0, 240 } }, { go = { 0, 300 } }, { report = "blue key" },
        { go = { 56, 275 }, teleports = true }, { wait = 0.5 }, { shot = "l2_teleported" }, { report = "teleported" },
        { go = { 0, 16 } }, { face = { 0, -4 } }, { use = true }, { wait = 1.2 }, { go = { 0, -24 } }, { report = "secret" }, { shot = "l2_secret" },
        { go = { 0, 16 } }, { go = { 88, 64 } }, { face = { 100, 64 } }, { use = true }, { wait = 1.2 },
        { go = { 120, 64 } }, { go = { 190, 64 } }, { face = { 204, 64 } }, { use = true }, { wait = 1.2 },
        { go = { 220, 64 } }, { go = { 238, 64 } }, { face = { 248, 64 } }, { use = true }, { wait = 0.5 },
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
        { go = { 136, 340 } }, { go = { 136, 358 } }, { face = { 136, 368 } }, { use = true }, { wait = 0.5 },
        { report = "intermission" }, { shot = "l3_intermission" }, { click = { 0.5, 0.66 } }, { wait = 0.3 }, { click = { 0.5, 0.66 } }, { wait = 1 },
    },
    ["Level 4 - The Tripod"] = {
        { wait = 0.3 }, { shot = "l4_start" }, { report = "start" },
        { go = { -30, 60 } }, { go = { 30, 60 } }, { go = { 0, 60 } }, { report = "weapons" },
        { go = { 0, 76 } }, { face = { 0, 84 } }, { use = true }, { wait = 1.2 }, { go = { 0, 100 } }, { shot = "l4_arena" },
        { fight = 8 }, { shot = "l4_fight" },
        -- Only the boss still stands in the way: end it, so the gate test doesn't depend on aim.
        { killall = true }, { wait = 2 }, { report = "boss dead" }, { shot = "l4_gate" },
        { go = { 90, 200 } }, { go = { 90, 340 } }, { go = { 0, 365 } }, { go = { 0, 395 } }, { go = { 0, 412 } }, { face = { 0, 424 } }, { use = true }, { wait = 0.5 },
        { report = "victory" }, { shot = "l4_victory" }, { flag = "sawVictory" },
        { click = { 0.476, 0.895 } }, { wait = 1 },
    },
    ["Prefab Gallery"] = {
        { wait = 0.5 }, { shot = "gallery_a" }, { go = { 0, 100 } }, { shot = "gallery_b" }, { done = true },
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
local ignored = {}

local function log(...)
    local parts = {}
    for i, v in ipairs({ ... }) do parts[i] = tostring(v) end
    Debug.LogWarning("BOT " .. table.concat(parts, " "))
end

local function V(name) return Game.GetVariable(name) end

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
    if s.mark then
        advance = true
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
