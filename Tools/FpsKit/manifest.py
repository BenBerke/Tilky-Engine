"""Rebuilds art_manifest.json from the template's own Assets, so gen/ can run
without freedoom2.wad (extract.py writes the same file while extracting).

usage: python -I manifest.py <Assets folder> <manifest.json>
"""
import json
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import extract  # noqa: E402
from texture_folders import all_paths  # noqa: E402


def png_size(path):
    with open(path, "rb") as f:
        head = f.read(24)
    return list(struct.unpack(">II", head[16:24]))


def actor(assets, reference):
    folder = os.path.join(assets, reference)
    files = [f[:-4] for f in os.listdir(folder) if f.endswith(".png")]
    frames = {}
    for name in files:
        frames[name[0]] = len(name) > 1  # A.png: one picture; A0..A7.png: rotated
    w, h = png_size(os.path.join(folder, sorted(files)[0] + ".png"))
    return {"w": w, "h": h, "frames": frames, "path": reference}


def main():
    assets, out = sys.argv[1], sys.argv[2]
    manifest = {"enemies": {}, "props": {}, "effects": {}, "pictures": {}, "words": {}}
    for name in extract.ENEMIES:
        manifest["enemies"][name] = actor(assets, "Sprites/Enemies/" + name)
    for group, things in extract.PROPS.items():
        for name in things:
            manifest["props"][name] = actor(assets, "Sprites/%s/%s" % (group, name))
    for name in extract.EFFECTS:
        manifest["effects"][name] = actor(assets, "Sprites/Effects/" + name)
    for name in extract.UI_PICS:
        manifest["pictures"][name] = png_size(os.path.join(assets, "UI", "Pictures", name + ".png"))
    for word in extract.WORDS:
        manifest["words"][word] = png_size(os.path.join(assets, "UI", "Words", word + ".png"))
    manifest["words"]["Tally Digit"] = png_size(os.path.join(assets, "UI", "Digits", "Tally 0.png"))
    # What fx_art.py adds after extract.py.
    for word in ["Options", "Gore", "Off", "Normal", "Extra"]:
        manifest["words"][word] = png_size(os.path.join(assets, "UI", "Words", word + ".png"))
    manifest["props"]["Grenade"] = {"w": 16, "h": 16, "path": "Sprites/FX/Grenade", "frames": {"Spin 1": False}}
    manifest["props"]["Grenade Box"] = {"w": 26, "h": 20, "path": "Sprites/FX/Grenade", "frames": {"Box": False}}

    manifest["textures"] = paths = all_paths(extract.WALLS, extract.FLATS, extract.SKIES)
    missing = [p for kind in paths.values() for p in kind.values() if not os.path.isfile(os.path.join(assets, p))]
    if missing:
        print("missing (run extract.py, or move them with manifest.py --move):", len(missing), missing[:5])

    if "--move" in sys.argv:
        # One-off: puts textures found anywhere under Textures into their folder.
        found = {}
        for root, _, names in os.walk(os.path.join(assets, "Textures")):
            for n in names:
                found.setdefault(n, []).append(os.path.join(root, n))
        for kind, old in (("wall", "Walls"), ("flat", "Flats"), ("sky", "Sky")):
            for name, path in paths[kind].items():
                src = os.path.join(assets, "Textures", old, name + ".png")
                dst = os.path.join(assets, path)
                if os.path.isfile(src) and os.path.abspath(src) != os.path.abspath(dst):
                    os.makedirs(os.path.dirname(dst), exist_ok=True)
                    os.replace(src, dst)
        for old in ("Walls", "Flats"):
            d = os.path.join(assets, "Textures", old)
            if os.path.isdir(d) and not os.listdir(d):
                os.rmdir(d)

    with open(out, "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=1)


if __name__ == "__main__":
    main()
