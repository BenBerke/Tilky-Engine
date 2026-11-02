-- Scripts/UI/RestartLevel.lua
--
-- ONE JOB: restarts whichever level is playing.
--
-- Why this is a script: a Load Level dispatch needs the level's name written in
-- it, but the death screen is shared by every level. A Call Function dispatch on
-- the Restart button calls this.

function RestartThisLevel()
    Game.RestartLevel()
end
