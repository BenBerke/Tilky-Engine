#!/bin/bash
# Copies the template, adds the bot and plays it. usage: run.sh <start level> <outDir> [maxFrames]
export PATH="/c/msys64/ucrt64/bin:$PATH"
T=/c/Users/berke/AppData/Local/Temp
REPO="/c/Users/berke/Desktop/CLion Projects/Wolfy Engine"
rm -rf "$T/fk/proj"
mkdir -p "$T/fk"
cp -r "$REPO/Templates/fps-kit/project" "$T/fk/proj"
mkdir -p "$T/fk/proj/Assets/Scripts/Test"
cp "$REPO/Tools/FpsKit/test/Bot.lua" "$T/fk/proj/Assets/Scripts/Test/Bot.lua"
cd "$T/tbj"
timeout 900 ./playtest.exe "C:/Users/berke/AppData/Local/Temp/fk/proj/project.tilky" "$1" "C:/Users/berke/AppData/Local/Temp/fk/$2" "${3:-40000}" </dev/null > "$T/fk/$2.log" 2>&1
echo "exit $?"
