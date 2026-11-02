"""Builds playtest.exe against the current build tree's objects.

usage: python -I build.py <build dir> <out exe>
(with C:/msys64/ucrt64/bin on PATH, after building Tilky_Engine)
"""
import os
import re
import subprocess
import sys

build, out = sys.argv[1], os.path.abspath(sys.argv[2]).replace("\\", "/")
here = os.path.dirname(os.path.abspath(__file__)).replace("\\", "/")
repo = os.path.abspath(os.path.join(here, "..", "..", "..")).replace("\\", "/")
os.chdir(build)

# Compiled again with hook.h. RuntimeSession.cpp is inside playtest.cpp.
HOOKED = ["src/Editor/Editor.cpp", "src/Runtime/Renderer/OpenGL/OpenGLInit.cpp", "src/Runtime/Renderer/OpenGL/OpenGL.cpp",
          "src/Engine/InputManager.cpp", "src/Engine/GameTime.cpp", "src/Runtime/Gameplay/UIButtonSystem.cpp"]
DROPPED = HOOKED + ["src/Runtime/RuntimeSession.cpp"]

text = open("build.ninja", encoding="utf-8").read()
entry = text[text.index("build CMakeFiles/Tilky_Engine.dir/Generated/TilkyUnifiedEntry.generated.cpp.obj:"):]


def var(block, name):
    return re.search(r"^  " + name + r" = (.*)$", block, re.M).group(1)


flags = re.sub(r"-include \S+", "", var(entry, "FLAGS")).replace("-fpch-preprocess", "").replace("-Winvalid-pch", "")
base = f'g++ {flags} {var(entry, "DEFINES")} {var(entry, "INCLUDES")} -include SDL3/SDL.h -include "{here}/hook.h"'

units = [(f"{here}/playtest.cpp", f"{out}.main.o")]
units += [(f"{repo}/{h}", f"{out}.hook{i}.o") for i, h in enumerate(HOOKED)]
only = os.environ.get("ONLY")
for source, obj in units:
    if only and only not in source:
        continue
    print("compiling", source, flush=True)
    if subprocess.run(f'{base} -c "{source}" -o "{obj}"', shell=True).returncode:
        sys.exit("compile failed: " + source)

exe = text[text.index("build Tilky_Engine.exe:"):]
head = exe.split("\n")[0]
objs = [o.replace("$ ", " ").replace("$:", ":") for o in re.findall(r"(CMakeFiles/(?:[^ $]|\$ |\$:)+?\.obj)", head)]
objs = [o for o in objs if "TilkyUnifiedEntry" not in o and not any(o.endswith("/" + d + ".obj") for d in DROPPED)]
libs = var(exe, "LINK_LIBRARIES")
link_flags = var(exe, "LINK_FLAGS") if re.search(r"^  LINK_FLAGS = ", exe, re.M) else ""

rsp = out + ".rsp"
with open(rsp, "w", encoding="utf-8") as f:
    for _, obj in units:
        f.write(f'"{obj}"\n')
    for o in objs:
        f.write(f'"{o}"\n')
    f.write(libs.replace("\\", "/") + "\n")

print("linking", flush=True)
wrapper = os.path.abspath("LldWrapper").replace("\\", "/")
if subprocess.run(f'g++ -g -fuse-ld=lld -Wl,--allow-multiple-definition -B"{wrapper}/" {link_flags} @"{rsp}" -o "{out}"', shell=True).returncode:
    sys.exit("link failed")
print("built", out)
