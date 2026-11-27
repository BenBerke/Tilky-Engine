"""Which folder under Assets/Textures each wall, flat and sky texture goes in.

Used by extract.py (writes the pictures there) and manifest.py (tells gen/ where
they are). Rules are tried in order, first match wins; names are Freedoom's lump
names. A wall and a flat may share a name (STEP1), so they must not end up in
the same folder.
"""
import fnmatch

# (kind, name patterns, folder). kind is "wall", "flat", "sky" or "*".
RULES = [
    ("sky", ["*"], "Sky"),

    ("*", ["SW1*", "SW2*"], "Switches"),
    ("wall", ["*DOOR*", "DOORSTOP", "DOORTRAK", "ZELDOOR"], "Doors"),
    ("flat", ["GATE*"], "Teleporters"),
    ("*", ["LITE*", "TLITE*", "GRNLITE*", "EXITSIGN"], "Lights"),

    ("wall", ["NUKE24", "NUKEDGE*", "BFALL*", "SFALL*", "WFALL*", "BLODRIP*"], "Liquids"),
    ("flat", ["FWATER*", "NUKAGE*", "LAVA*", "BLOOD*", "SLIME*"], "Liquids"),

    ("wall", ["SKIN*", "SKSNAKE*", "SKSPINE*", "SKULWALL", "SP_FACE*", "SP_HOT*", "MARBFACE", "REDWALL",
              "FIRE*", "SLAD*"], "Hell"),
    ("flat", ["DEM1_*", "SFLR*"], "Hell"),

    ("*", ["CRAT*"], "Crates"),
    ("wall", ["WOOD*", "PANEL*"], "Wood"),
    ("wall", ["STARTAN*", "STARG*", "STARBR*", "COMP*", "TEK*", "SPACEW*", "PLAT*", "SILVER*", "MODWALL*",
              "ICKWALL*"], "Technology"),
    ("flat", ["COMP*", "CONS*"], "Technology"),
    ("wall", ["METAL*", "SUPPORT*", "SHAWN*", "PIPE*", "BROWNPIP", "MIDBARS*", "MIDBRN*", "MIDGRATE", "MIDSPACE"], "Metal"),
    ("wall", ["BRICK*", "BIGBRIK*"], "Brick"),
    ("wall", ["STEP*"], "Steps"),
    ("wall", ["ROCK*", "TANROCK*", "SP_ROCK*"], "Nature"),
    ("flat", ["RROCK*", "GRNROCK", "GRASS*"], "Nature"),
    ("wall", ["STONE*", "GSTONE*", "MARBLE*", "CEMENT*", "GRAY*", "STUCCO*", "BROWN*", "EXITSTON", "ZIMMER*"], "Stone"),

    ("flat", ["CEIL*"], "Ceilings"),
    ("flat", ["*"], "Floors"),
]


def folder_for(kind, name):
    for rule_kind, patterns, folder in RULES:
        if rule_kind in ("*", kind) and any(fnmatch.fnmatchcase(name, p) for p in patterns):
            return folder
    raise KeyError("no texture folder for %s %s: add a rule to texture_folders.py" % (kind, name))


def path_for(kind, name):
    """The texture's path from the Assets folder, as levels store it."""
    return "Textures/%s/%s.png" % (folder_for(kind, name), name)


def all_paths(walls, flats, skies):
    """{"wall": {name: path}, "flat": {...}, "sky": {...}}; fails if two land on one file."""
    out, used = {}, {}
    for kind, names in (("wall", walls), ("flat", flats), ("sky", skies)):
        out[kind] = {}
        for name in names:
            path = path_for(kind, name)
            if path in used:
                raise ValueError("%s %s and %s both go to %s" % (kind, name, used[path], path))
            used[path] = "%s %s" % (kind, name)
            out[kind][name] = path
    return out
