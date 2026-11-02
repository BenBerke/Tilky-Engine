"""Builds fpskit.exe against the current build tree's objects.

usage: python -I build.py <build dir> <out exe> [main.cpp]
(with C:/msys64/ucrt64/bin on PATH, after building Tilky_Engine)

Takes the compile flags and link line from build.ninja, so it's the editor build
with main swapped out. An optional other main.cpp builds a test harness instead.
"""
import os
import re
import subprocess
import sys

build, out = sys.argv[1], os.path.abspath(sys.argv[2])
here = os.path.dirname(os.path.abspath(__file__))
source = os.path.abspath(sys.argv[3]) if len(sys.argv) > 3 else os.path.join(here, "main.cpp")
extra = sys.argv[4] if len(sys.argv) > 4 else ""
os.chdir(build)

text = open("build.ninja", encoding="utf-8").read()
entry = text[text.index("build CMakeFiles/Tilky_Engine.dir/Generated/TilkyUnifiedEntry.generated.cpp.obj:"):]


def var(block, name):
    return re.search(r"^  " + name + r" = (.*)$", block, re.M).group(1)


flags = re.sub(r"-include \S+", "", var(entry, "FLAGS")).replace("-fpch-preprocess", "").replace("-Winvalid-pch", "")
compile_line = (f'g++ {flags} {var(entry, "DEFINES")} {var(entry, "INCLUDES")} -I"{here}" -include SDL3/SDL.h {extra} '
                f'-c "{source}" -o "{out}.o"')
print("compiling", source)
if subprocess.run(compile_line, shell=True).returncode:
    sys.exit("compile failed")

exe = text[text.index("build Tilky_Engine.exe:"):]
head = exe.split("\n")[0]
objs = [o.replace("$ ", " ").replace("$:", ":") for o in re.findall(r"(CMakeFiles/(?:[^ $]|\$ |\$:)+?\.obj)", head)]
objs = [o for o in objs if "TilkyUnifiedEntry" not in o]
libs = var(exe, "LINK_LIBRARIES")
link_flags = var(exe, "LINK_FLAGS") if re.search(r"^  LINK_FLAGS = ", exe, re.M) else ""

rsp = out + ".rsp"
with open(rsp, "w", encoding="utf-8") as f:
    f.write('"' + (out + ".o").replace("\\", "/") + '"\n')
    for o in objs:
        f.write('"' + o + '"\n')
    f.write(libs.replace("\\", "/") + "\n")

link = f'g++ -g -fuse-ld=lld -B"{os.path.abspath("LldWrapper")}/" {link_flags} @"{rsp}" -o "{out}"'
print("linking")
if subprocess.run(link, shell=True).returncode:
    sys.exit("link failed")
print("built", out)
