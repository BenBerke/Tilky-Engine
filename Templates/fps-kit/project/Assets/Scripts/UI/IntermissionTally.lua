-- Scripts/UI/IntermissionTally.lua
--
-- ONE JOB: counts up the score on the screen between levels, one line at a time, with
-- Doom's rattle of shots. A click or a key skips to the end, and the next one continues.
--
-- Why this is a script: dispatches can't count up over time with a sound on each step.
--
-- It writes the Tally... variables, and PictureNumber.lua draws them.

public string tickSound = "Sounds/Weapons/pistol.wav"
public string doneSound = "Sounds/World/barrel explode.wav"

local PERCENT_PER_SECOND = 70  -- Doom's speed
local SECONDS_PER_SECOND = 90
local TICK_SECONDS = 0.11
local PAUSE_BETWEEN_LINES = 0.3

local lines = nil
local current = 1
local shown = 0
local pause = 0
local tick = 0
local finished = false

local function Percent(countVariable, totalVariable)
    local total = Game.GetVariable(totalVariable)
    if total <= 0 then return 100 end
    return math.floor(Game.GetVariable(countVariable) * 100 / total)
end

local function WriteClock(seconds)
    Game.SetVariable("TallyMinutes", math.floor(seconds / 60))
    Game.SetVariable("TallySeconds", math.floor(seconds % 60))
end

local function Begin()
    lines = {
        { variable = "TallyKills", target = Percent("Kills", "TotalKills"), speed = PERCENT_PER_SECOND },
        { variable = "TallyItems", target = Percent("Items", "TotalItems"), speed = PERCENT_PER_SECOND },
        { variable = "TallySecrets", target = Percent("Secrets", "TotalSecrets"), speed = PERCENT_PER_SECOND },
        { clock = true, target = math.floor(Game.GetVariable("Time")), speed = SECONDS_PER_SECOND },
    }
    for _, name in ipairs({ "TallyKills", "TallyItems", "TallySecrets", "TallyMinutes", "TallySeconds", "TallyDone", "TallySkip" }) do
        Game.SetVariable(name, 0)
    end

    Game.MessageBox.Clear() -- the last pickup message doesn't belong on this screen

    -- "Level 1 - Outpost" reads better as "OUTPOST" here.
    local title = Game.FindEntity("Level Title")
    if title ~= nil then
        title.uiText.text = string.upper((string.gsub(Game.levelName, "^Level %d+ %- ", "")))
    end
end

local function Write(line, value)
    if line.clock then WriteClock(value) else Game.SetVariable(line.variable, value) end
end

local function Finish()
    if finished then return end
    finished = true
    for _, line in ipairs(lines) do Write(line, line.target) end
    Game.SetVariable("TallyDone", 1)
    Audio.Play2D(doneSound)
    local prompt = Game.FindEntity("Continue Prompt")
    if prompt ~= nil then prompt.uiSprite.isActive = true end
end

function Update()
    if lines == nil then Begin() end

    local keyPressed = Input.GetKeyDown(Key.Enter) or Input.GetKeyDown(Key.Space)

    if finished then
        if keyPressed then Game.Signal("Menu Pick Next Level") end
        return
    end

    if keyPressed or Game.GetVariable("TallySkip") ~= 0 then Finish() return end

    local dt = GameTime.unscaledDeltaTime
    if pause > 0 then pause = pause - dt return end

    local line = lines[current]
    shown = math.min(line.target, shown + line.speed * dt)
    Write(line, math.floor(shown))

    tick = tick - dt
    if tick <= 0 then
        tick = TICK_SECONDS
        Audio.Play2D(tickSound)
    end

    if shown >= line.target then
        current = current + 1
        shown = 0
        pause = PAUSE_BETWEEN_LINES
        if current > #lines then Finish() end
    end
end
