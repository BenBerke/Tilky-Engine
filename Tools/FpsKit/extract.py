"""Extracts the Classic FPS Kit's art and sound from freedoom2.wad into the
template's Assets folder and writes art_manifest.json for gen/ (the prefab and
level generator).

usage: python -I extract.py <freedoom2.wad> <Assets folder> <manifest.json>

Everything here is Freedoom's (BSD-3-Clause): see Assets/Freedoom/COPYING.txt."""
import json
import os
import shutil
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from letters import GLYPHS, draw_word, rows_of  # noqa: E402
from ttf import write_pixel_font  # noqa: E402
from wad import Wad, decode_picture  # noqa: E402

# ── What to extract ─────────────────────────────────────────────────────────

# Folder under Sprites/Enemies -> (lump prefix, frame letters to keep). Rotated
# frames stay rotated; gibs are left out. Doom's own frame order.
ENEMIES = {
    "Zombie": ("POSS", "ABCDEFGHIJKL"),
    "Shotgun Zombie": ("SPOS", "ABCDEFGHIJKL"),
    "Minigun Zombie": ("CPOS", "ABCDEFGHIJKLMN"),
    "Serpentipede": ("TROO", "ABCDEFGHIJKLM"),
    "Flesh Worm": ("SARG", "ABCDEFGHIJKLMN"),
    "Trilobite": ("HEAD", "ABCDEFGHIJKL"),
    "Deadflare": ("SKUL", "ABCDEFGHIJK"),
    "Pain Bringer": ("BOS2", "ABCDEFGHIJKLMNO"),
    "Pain Lord": ("BOSS", "ABCDEFGHIJKLMNO"),
    "Hangman": ("SKEL", "ABCDEFGHIJKLMNOPQ"),
    "Combat Slug": ("FATT", "ABCDEFGHIJKLMNOPQRST"),
    "Technospider": ("BSPI", "ABCDEFGHIJKLMNOP"),
    "Matribite": ("PAIN", "ABCDEFGHIJKLM"),
    "Assault Tripod": ("CYBR", "ABCDEFGHIJKLMNOP"),
    "Large Technospider": ("SPID", "ABCDEFGHIJKLMNOPQRS"),
}

# Floor-standing things: folder under Sprites -> {name: lump prefix}.
PROPS = {
    "Pickups": {
        "Clip": "CLIP", "Ammo Box": "AMMO", "Shells": "SHEL", "Shell Box": "SBOX", "Rocket": "ROCK",
        "Rocket Box": "BROK", "Cell": "CELL", "Cell Pack": "CELP", "Backpack": "BPAK",
        "Stimpack": "STIM", "Medikit": "MEDI", "Health Bonus": "BON1", "Armor Bonus": "BON2",
        "Soul Sphere": "SOUL", "Mega Sphere": "MEGA", "Green Armor": "ARM1", "Blue Armor": "ARM2",
        "Red Key": "RKEY", "Blue Key": "BKEY", "Yellow Key": "YKEY",
        "Red Skull": "RSKU", "Blue Skull": "BSKU", "Yellow Skull": "YSKU",
        "Berserk": "PSTR", "Invulnerability": "PINV", "Invisibility": "PINS", "Radiation Suit": "SUIT",
        "Light Goggles": "PVIS", "Area Map": "PMAP",
        "Chainsaw": "CSAW", "Pistol": "PIST", "Shotgun": "SHOT", "Super Shotgun": "SGN2",
        "Minigun": "MGUN", "Rocket Launcher": "LAUN", "Plasma Rifle": "PLAS", "SKAG 1337": "BFUG",
    },
    "Decorations": {
        "Barrel": "BAR1", "Burning Barrel": "FCAN", "Candle": "CAND", "Candelabra": "CBRA",
        "Tall Lamp": "TLMP", "Short Lamp": "TLP2", "Column": "COLU", "Tall Green Pillar": "COL1",
        "Short Green Pillar": "COL2", "Tall Red Pillar": "COL3", "Short Red Pillar": "COL4",
        "Heart Pillar": "COL5", "Skull Pillar": "COL6", "Tech Pillar": "ELEC", "Evil Eye": "CEYE",
        "Floating Skull": "FSKU", "Blue Torch": "TBLU", "Green Torch": "TGRN", "Red Torch": "TRED",
        "Short Blue Torch": "SMBT", "Short Green Torch": "SMGT", "Short Red Torch": "SMRT",
        "Burnt Tree": "TRE1", "Big Tree": "TRE2", "Stump": "SMIT", "Skull Pile": "POL5", "Impaled": "POL1",
        "Skull On Pole": "POL4", "Skulls On Pole": "POL2", "Hanging Body": "GOR2", "Hanging Legs": "GOR3",
        "Hanging Torso": "GOR4", "Hanging Twitch": "GOR1", "Blood Pool": "POB1", "Small Blood Pool": "POB2", "Gore Pile": "BRS1",
    },
}

# Centred effects: name -> lump prefix, frame letters.
EFFECTS = {
    "Puff": ("PUFF", "ABCD"), "Blood": ("BLUD", "ABC"),
    "Fireball": ("BAL1", "ABCDE"), "Plasma Ball Red": ("BAL2", "ABCDE"), "Plasma Ball Green": ("BAL7", "ABCDE"),
    "Rocket": ("MISL", "ABCD"), "Plasma": ("PLSS", "AB"), "Plasma Hit": ("PLSE", "ABCDE"),
    "SKAG Ball": ("BFS1", "AB"), "SKAG Hit": ("BFE1", "ABCDEF"), "Tracer": ("FATB", "AB"), "Tracer Hit": ("FBXP", "ABC"),
    "Slug Ball": ("MANF", "AB"), "Spider Plasma": ("APLS", "AB"), "Spider Hit": ("APBX", "ABCDE"),
    "Barrel Explosion": ("BEXP", "ABCDE"), "Teleport Fog": ("TFOG", "ABCDEFGHIJ"), "Item Fog": ("IFOG", "ABCDE"),
}

# Player weapons: drawn on a 320x168 canvas the way Doom draws them above its
# status bar. name -> {frame name: [lumps, drawn in order]}.
WEAPONS = {
    "Fist": {"Ready": ["PUNGA0"], "B": ["PUNGB0"], "C": ["PUNGC0"], "D": ["PUNGD0"]},
    "Chainsaw": {"Ready1": ["SAWGC0"], "Ready2": ["SAWGD0"], "Fire1": ["SAWGA0"], "Fire2": ["SAWGB0"]},
    "Pistol": {"Ready": ["PISGA0"], "B": ["PISGB0"], "C": ["PISGC0"], "Flash": ["PISFA0", "PISGB0"]},
    "Shotgun": {"Ready": ["SHTGA0"], "B": ["SHTGB0"], "C": ["SHTGC0"], "D": ["SHTGD0"],
                "Flash1": ["SHTFA0", "SHTGA0"], "Flash2": ["SHTFB0", "SHTGA0"]},
    "Super Shotgun": {"Ready": ["SHT2A0"], "B": ["SHT2B0"], "C": ["SHT2C0"], "D": ["SHT2D0"], "E": ["SHT2E0"],
                      "F": ["SHT2F0"], "G": ["SHT2G0"], "H": ["SHT2H0"],
                      "Flash1": ["SHT2I0", "SHT2A0"], "Flash2": ["SHT2J0", "SHT2A0"]},
    "Minigun": {"Ready": ["CHGGA0"], "B": ["CHGGB0"], "Flash1": ["CHGFA0", "CHGGA0"], "Flash2": ["CHGFB0", "CHGGB0"]},
    "Rocket Launcher": {"Ready": ["MISGA0"], "B": ["MISGB0"], "Flash1": ["MISFA0", "MISGB0"], "Flash2": ["MISFB0", "MISGB0"],
                        "Flash3": ["MISFC0", "MISGB0"], "Flash4": ["MISFD0", "MISGB0"]},
    "Plasma Rifle": {"Ready": ["PLSGA0"], "B": ["PLSGB0"], "Flash1": ["PLSFA0", "PLSGA0"], "Flash2": ["PLSFB0", "PLSGA0"]},
    "SKAG 1337": {"Ready": ["BFGGA0"], "B": ["BFGGB0"], "C": ["BFGGC0"], "Flash1": ["BFGFA0", "BFGGB0"], "Flash2": ["BFGFB0", "BFGGB0"]},
}

# Pixels a weapon sits above Doom's spot. Freedoom's Super Shotgun is drawn so low
# that the status bar hides most of it, and firing's picture kick pushes it off screen.
WEAPON_LIFT = {"Super Shotgun": 12}

WALLS = """STARTAN1 STARTAN2 STARTAN3 STARG1 STARG2 STARG3 STARGR1 STARBR1 BROWN1 BROWN96 BROWN144 BROWNGRN BROWNHUG
BROWNPIP GRAY1 GRAY2 GRAY4 GRAY5 GRAY7 GRAYBIG GRAYTALL GRAYVINE BIGBRIK1 BIGBRIK2 BIGDOOR1 BIGDOOR2 BIGDOOR3 BIGDOOR4
BIGDOOR5 BIGDOOR6 BIGDOOR7 BRICK1 BRICK2 BRICK3 BRICK4 BRICK5 BRICK6 BRICK7 BRICK8 BRICK9 BRICK10 BRICK11 BRICK12
BRICKLIT CEMENT1 CEMENT2 CEMENT3 CEMENT4 CEMENT5 CEMENT6 CEMENT7 CEMENT8 CEMENT9 COMPBLUE COMPSPAN COMPSTA1 COMPSTA2
COMPTALL COMPTILE COMPUTE1 COMPUTE2 COMPUTE3 COMP2 CRATE1 CRATE2 CRATE3 CRATELIT CRATINY CRATWIDE DOOR1 DOOR3 DOORBLU
DOORBLU2 DOORHI DOORRED DOORRED2 DOORSTOP DOORTRAK DOORYEL DOORYEL2 EXITDOOR EXITSIGN EXITSTON FIREBLU1 FIREBLU2
FIRELAVA FIRELAV3 FIREWALA FIREWALB FIREWALL GSTONE1 GSTONE2 ICKWALL1 ICKWALL2 ICKWALL3 LITE3 LITE5 LITE96 LITEBLU1
LITEBLU4 LITERED LITEYEL1 MARBFACE MARBLE1 MARBLE2 MARBLE3 METAL METAL1 METAL2 METAL3 METAL4 METAL5 METAL6 METAL7
MIDBARS1 MIDBARS3 MIDBRN1 MIDGRATE MIDSPACE MODWALL1 MODWALL2 MODWALL3 MODWALL4 NUKE24 NUKEDGE1 PANEL1 PANEL2 PANEL3
PANEL4 PANEL5 PANEL6 PANEL7 PANEL8 PANEL9 PIPE1 PIPE2 PIPE3 PIPE4 PIPE6 PIPEWAL1 PIPEWAL2 PLAT1 REDWALL ROCK1 ROCK2
ROCK3 ROCK4 ROCK5 ROCKRED1 ROCKRED2 ROCKRED3 SHAWN1 SHAWN2 SHAWN3 SILVER1 SILVER2 SILVER3 SKIN2 SKINFACE SKSNAKE1
SKSPINE1 SKULWALL SLADWALL SLAD2 SLAD3 SLADPOIS SP_ROCK1 SP_HOT1 SP_FACE1 SPACEW2 SPACEW3 SPACEW4 SPCDOOR1 SPCDOOR2
SPCDOOR3 STEP1 STEP2 STEP3 STEP4 STEP5 STEP6 STEPLAD1 STEPTOP STONE STONE2 STONE3 STONE4 STONE5 STONE6 STONE7 STUCCO
SUPPORT2 SUPPORT3 SW1BRN1 SW2BRN1 SW1COMM SW2COMM SW1EXIT SW2EXIT SW1GRAY SW2GRAY SW1METAL SW2METAL SW1STRTN SW2STRTN
SW1COMP SW2COMP SW1TEK SW2TEK SW1BLUE SW2BLUE SW1SKIN SW2SKIN SW1HOT SW2HOT TANROCK2 TANROCK3 TANROCK4 TANROCK5
TEKBRON1 TEKGREN1 TEKGREN2 TEKGREN3 TEKGREN4 TEKLITE TEKWALL1 TEKWALL2 TEKWALL3 TEKWALL4 TEKWALL5 TEKWALL6 WOOD1 WOOD3
WOOD5 WOOD6 WOOD9 WOODMET1 ZIMMER1 ZIMMER2 ZIMMER3 ZDOORF1 ZDOORB1 ZELDOOR BFALL1 BFALL2 BFALL3 BFALL4 SFALL1 SFALL2
SFALL3 SFALL4 WFALL1 WFALL2 WFALL3 WFALL4 BLODRIP1 BLODRIP2 BLODRIP3 BLODRIP4""".split()

SKIES = ["SKY1", "SKY2", "SKY3"]

FLATS = """FLAT1 FLAT1_1 FLAT1_2 FLAT1_3 FLAT2 FLAT3 FLAT4 FLAT5 FLAT5_1 FLAT5_2 FLAT5_3 FLAT5_4 FLAT5_5 FLAT5_6 FLAT5_7
FLAT5_8 FLAT8 FLAT9 FLAT10 FLAT14 FLAT17 FLAT18 FLAT19 FLAT20 FLAT22 FLAT23 FLOOR0_1 FLOOR0_2 FLOOR0_3 FLOOR0_5 FLOOR0_6
FLOOR0_7 FLOOR1_1 FLOOR1_6 FLOOR1_7 FLOOR3_3 FLOOR4_1 FLOOR4_5 FLOOR4_6 FLOOR4_8 FLOOR5_1 FLOOR5_2 FLOOR5_3 FLOOR5_4
FLOOR6_1 FLOOR6_2 FLOOR7_1 FLOOR7_2 CEIL1_1 CEIL1_2 CEIL1_3 CEIL3_1 CEIL3_2 CEIL3_3 CEIL3_4 CEIL3_5 CEIL3_6 CEIL4_1
CEIL4_2 CEIL4_3 CEIL5_1 CEIL5_2 COMP01 CONS1_1 CONS1_5 CONS1_7 CRATOP1 CRATOP2 DEM1_1 DEM1_2 DEM1_3 DEM1_4 DEM1_5 DEM1_6
GATE1 GATE2 GATE3 GATE4 GRASS1 GRASS2 GRNLITE1 GRNROCK MFLR8_1 MFLR8_2 MFLR8_3 MFLR8_4 RROCK01 RROCK02 RROCK03 RROCK04
RROCK09 RROCK10 RROCK11 RROCK12 RROCK13 RROCK14 RROCK16 RROCK17 RROCK18 RROCK19 RROCK20 SFLR6_1 SFLR6_4 SFLR7_1 SFLR7_4
SLIME01 SLIME02 SLIME03 SLIME04 SLIME09 SLIME10 SLIME11 SLIME12 STEP1 STEP2 TLITE6_1 TLITE6_4 TLITE6_5 TLITE6_6 NUKAGE1
NUKAGE2 NUKAGE3 FWATER1 FWATER2 FWATER3 FWATER4 LAVA1 LAVA2 LAVA3 LAVA4 BLOOD1 BLOOD2 BLOOD3""".split()

UI_PICS = """TITLEPIC INTERPIC BOSSBACK STBAR STARMS M_DOOM M_SKULL1 M_SKULL2 STKEYS0 STKEYS1 STKEYS2""".split()

# Number pictures, each set on one canvas so a narrow "1" isn't stretched.
DIGITS = {
    "Big": ["STTNUM%d" % n for n in range(10)] + ["STTPRCNT"],
    "Yellow": ["STYSNUM%d" % n for n in range(10)],
    "Gray": ["STGNUM%d" % n for n in range(10)],
}

# The status bar faces. Doom places them with their offsets on a 33 x 32 spot of the bar.
FACES = ["STFST%d%d" % (band, look) for band in range(5) for look in range(3)] + \
        ["STFTL%d0" % band for band in range(5)] + ["STFTR%d0" % band for band in range(5)] + \
        ["STFOUCH%d" % band for band in range(5)] + ["STFEVL%d" % band for band in range(5)] + \
        ["STFKILL%d" % band for band in range(5)] + ["STFDEAD0", "STFGOD0"]

# Every word the menus show, drawn with letters.py. Add one here to use it in a layout.
WORDS = ["New Game", "Select Level", "Quit Game", "Resume", "Restart Level", "Main Menu", "Try Again", "You Died",
         "You Win", "Continue", "Back", "Choose Level", "Outpost", "Keys", "Courtyard", "The Tripod", "Finished",
         "Kills", "Items", "Secret", "Time"]

# Sound lumps grouped into plain, readable names.
SOUNDS = {
    "Weapons": {"DSPISTOL": "pistol", "DSSHOTGN": "shotgun", "DSSGCOCK": "shotgun cock", "DSDSHTGN": "super shotgun",
                "DSDBOPN": "super shotgun open", "DSDBLOAD": "super shotgun load", "DSDBCLS": "super shotgun close",
                "DSRLAUNC": "rocket launcher", "DSRXPLOD": "rocket explode", "DSPLASMA": "plasma", "DSBFG": "skag charge",
                "DSPUNCH": "punch", "DSSAWUP": "chainsaw start", "DSSAWIDL": "chainsaw idle", "DSSAWFUL": "chainsaw swing",
                "DSSAWHIT": "chainsaw hit", "DSWPNUP": "weapon pickup"},
    "World": {"DSDOROPN": "door open", "DSDORCLS": "door close", "DSBDOPN": "fast door open", "DSBDCLS": "fast door close",
              "DSPSTART": "lift start", "DSPSTOP": "lift stop", "DSSTNMOV": "floor move", "DSSWTCHN": "switch on",
              "DSSWTCHX": "switch exit", "DSTELEPT": "teleport", "DSBAREXP": "barrel explode", "DSFIRSHT": "fireball shoot",
              "DSFIRXPL": "fireball explode", "DSFLAME": "fire", "DSFLAMST": "fire start", "DSMETAL": "metal step",
              "DSRADIO": "radio", "DSTINK": "tink"},
    "Player": {"DSOOF": "oof", "DSNOWAY": "no way", "DSPLPAIN": "pain", "DSPLDETH": "death", "DSPDIEHI": "death loud",
               "DSJUMP": "jump", "DSSLOP": "gib"},
    "Items": {"DSITEMUP": "item pickup", "DSGETPOW": "powerup pickup", "DSITMBK": "item respawn"},
    "Monsters": {"DSPOSIT1": "zombie sight 1", "DSPOSIT2": "zombie sight 2", "DSPOSIT3": "zombie sight 3",
                 "DSPODTH1": "zombie death 1", "DSPODTH2": "zombie death 2", "DSPODTH3": "zombie death 3",
                 "DSPOPAIN": "zombie pain", "DSPOSACT": "zombie active", "DSBGSIT1": "serpentipede sight 1",
                 "DSBGSIT2": "serpentipede sight 2", "DSBGDTH1": "serpentipede death 1", "DSBGDTH2": "serpentipede death 2",
                 "DSBGACT": "serpentipede active", "DSCLAW": "claw", "DSSGTSIT": "worm sight", "DSSGTATK": "worm attack",
                 "DSSGTDTH": "worm death", "DSDMPAIN": "worm pain", "DSDMACT": "worm active", "DSCACSIT": "trilobite sight",
                 "DSCACDTH": "trilobite death", "DSSKLATK": "deadflare attack", "DSSKLDTH": "deadflare death",
                 "DSKNTSIT": "pain bringer sight", "DSKNTDTH": "pain bringer death", "DSBRSSIT": "pain lord sight",
                 "DSBRSDTH": "pain lord death", "DSSKESIT": "hangman sight", "DSSKEDTH": "hangman death",
                 "DSSKEACT": "hangman active", "DSSKEATK": "hangman shoot", "DSSKESWG": "hangman swing",
                 "DSSKEPCH": "hangman punch", "DSMANSIT": "slug sight", "DSMANDTH": "slug death", "DSMANATK": "slug shoot",
                 "DSMNPAIN": "slug pain", "DSBSPSIT": "spider sight", "DSBSPDTH": "spider death", "DSBSPACT": "spider active",
                 "DSBSPWLK": "spider walk", "DSPESIT": "matribite sight", "DSPEPAIN": "matribite pain",
                 "DSPEDTH": "matribite death", "DSCYBSIT": "tripod sight", "DSCYBDTH": "tripod death", "DSHOOF": "tripod hoof",
                 "DSSPISIT": "large spider sight", "DSSPIDTH": "large spider death"},
}

# ── PNG ─────────────────────────────────────────────────────────────────────


def write_png(path, w, h, rgba):
    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    raw = bytearray()
    stride = w * 4
    for y in range(h):
        raw.append(0)
        raw += rgba[y * stride:(y + 1) * stride]
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(bytes(raw), 9)))
        f.write(chunk(b"IEND", b""))


def indices_to_rgba(pal, w, h, pixels, flip=False):
    out = bytearray(w * h * 4)
    for y in range(h):
        for x in range(w):
            c = pixels[y * w + (w - 1 - x if flip else x)]
            if c >= 0:
                r, g, b = pal[c]
                i = (y * w + x) * 4
                out[i], out[i + 1], out[i + 2], out[i + 3] = r, g, b, 255
    return out


def blit(canvas, cw, ch, pic, x0, y0, flip=False):
    """Draws a decoded picture onto a palette-index canvas."""
    w, h, _, _, pixels = pic
    for y in range(h):
        ty = y0 + y
        if ty < 0 or ty >= ch:
            continue
        for x in range(w):
            tx = x0 + x
            if tx < 0 or tx >= cw:
                continue
            c = pixels[y * w + (w - 1 - x if flip else x)]
            if c >= 0:
                canvas[ty * cw + tx] = c


# ── Sprites ─────────────────────────────────────────────────────────────────


def sprite_frames(wad, prefix):
    """{frame letter: {doom rotation 0-8: (lump, flipped)}}, from names like
    TROOA1 and TROOA2A8."""
    frames = {}
    for name in wad.names_between("S_START", "S_END"):
        if not name.startswith(prefix) or len(name) < 6:
            continue
        frames.setdefault(name[4], {})[int(name[5])] = (name, False)
        if len(name) >= 8:
            frames.setdefault(name[6], {})[int(name[7])] = (name, True)
    return frames


def extract_actor(wad, prefix, letters, out_dir, reference, centred=False):
    """Writes every frame on one canvas, so the feet (or centre, for effects)
    stay in place from frame to frame. Doom rotation N goes in Tilky's 8-sided
    sprite slot (9 - N) % 8. Returns the manifest entry."""
    frames = sprite_frames(wad, prefix)
    if letters is None:
        letters = "".join(sorted(frames))
    pics = {}
    half_w, up, down = 1, 1, 0
    for letter in letters:
        if letter not in frames:
            raise KeyError(prefix + letter)
        for rot, (lump, _) in frames[letter].items():
            if lump not in pics:
                pics[lump] = wad.picture(lump)
            w, h, left, top, _ = pics[lump]
            half_w = max(half_w, abs(left), abs(w - left))
            up = max(up, top)
            down = max(down, h - top)
    if centred:
        up = down = max(up, down)
    else:
        down = 0  # below the floor: cut off, as Doom's floor would
    cw, ch = half_w * 2, up + down

    manifest = {"w": cw, "h": ch, "frames": {}}
    for letter in letters:
        rots = frames[letter]
        rotated = 0 not in rots
        manifest["frames"][letter] = rotated
        for rot, (lump, flipped) in rots.items():
            w, h, left, top, _ = pics[lump]
            canvas = [-1] * (cw * ch)
            # A mirrored lump's left offset mirrors too.
            x0 = half_w - (w - left if flipped else left)
            blit(canvas, cw, ch, pics[lump], x0, up - top, flipped)
            slot = (9 - rot) % 8 if rotated else None
            name = letter + (str(slot) if rotated else "") + ".png"
            write_png(os.path.join(out_dir, name), cw, ch, indices_to_rgba(wad.palette, cw, ch, canvas))
    manifest["path"] = reference
    return manifest


def extract_weapon(wad, frames, out_dir, lift=0):
    for frame, lumps in frames.items():
        cw, ch = 320, 168
        canvas = [-1] * (cw * ch)
        for lump in lumps:
            pic = wad.picture(lump)
            # R_DrawPSprite: x = 1 - leftoffset, y = 32 - topoffset (WEAPONTOP).
            blit(canvas, cw, ch, pic, 1 - pic[2], 32 - pic[3] - lift)
        write_png(os.path.join(out_dir, frame + ".png"), cw, ch, indices_to_rgba(wad.palette, cw, ch, canvas))


# ── The status bar and menus ────────────────────────────────────────────────


def write_digits(wad, assets):
    """UI/Digits/<set> <0-9>.png, plus "percent" and "colon" for the sets that have them."""
    names = {"STTPRCNT": "percent"}
    for name, lumps in DIGITS.items():
        pics = [wad.picture(lump) for lump in lumps]
        w, h = max(p[0] for p in pics), max(p[1] for p in pics)
        for lump, pic in zip(lumps, pics):
            canvas = [-1] * (w * h)
            blit(canvas, w, h, pic, w - pic[0], h - pic[1])  # right-aligned, like Doom's numbers
            label = names.get(lump, lump[-1])
            write_png(os.path.join(assets, "UI", "Digits", "%s %s.png" % (name, label)), w, h,
                      indices_to_rgba(wad.palette, w, h, canvas))


def write_faces(wad, assets):
    for lump in FACES:
        pic = wad.picture(lump)
        canvas = [-1] * (33 * 32)
        blit(canvas, 33, 32, pic, -pic[2], -pic[3])
        write_png(os.path.join(assets, "UI", "Faces", lump + ".png"), 33, 32, indices_to_rgba(wad.palette, 33, 32, canvas))


def write_bar_side(wad, assets):
    """What fills the space beside the status bar on a wide screen: Doom's border flat."""
    fw, fh, flat = wad.flat("FLOOR7_2")
    w, h = 40, 32
    canvas = [flat[(y % fh) * fw + (x % fw)] for y in range(h) for x in range(w)]
    write_png(os.path.join(assets, "UI", "Pictures", "BARSIDE.png"), w, h, indices_to_rgba(wad.palette, w, h, canvas))


def write_tally_digits(assets, manifest):
    """The score numbers between levels, in the same letters as the words above them."""
    labels = {str(n): str(n) for n in range(10)}
    labels.update({"%": "percent", ":": "colon"})
    drawn = {char: draw_word(char) for char in labels}
    w, h = max(d[0] for d in drawn.values()), max(d[1] for d in drawn.values())
    for char, (dw, dh, rgba) in drawn.items():
        canvas = bytearray(w * h * 4)
        for y in range(dh):
            start = (y * w + (w - dw)) * 4  # right-aligned, like the bar's numbers
            canvas[start:start + dw * 4] = rgba[y * dw * 4:(y + 1) * dw * 4]
        write_png(os.path.join(assets, "UI", "Digits", "Tally %s.png" % labels[char]), w, h, bytes(canvas))
    manifest.setdefault("words", {})["Tally Digit"] = [w, h]


def write_words(assets, manifest):
    for word in WORDS:
        w, h, rgba = draw_word(word)
        write_png(os.path.join(assets, "UI", "Words", word + ".png"), w, h, rgba)
        manifest.setdefault("words", {})[word] = [w, h]


def write_font(assets):
    """The same letters as the menu words, for messages and labels."""
    glyphs = {" ": (4, [])}
    for char in GLYPHS:
        rows = rows_of(char)
        glyphs[char] = (len(rows[0]) + 1, rows)
        if char.isalpha():
            glyphs[char.lower()] = glyphs[char]
    write_pixel_font(os.path.join(assets, "Fonts", "Classic HUD.ttf"), "Classic HUD", glyphs, height=8, descent=0)


# ── Main ────────────────────────────────────────────────────────────────────


def main():
    wad = Wad(sys.argv[1])
    assets = sys.argv[2]
    manifest = {"enemies": {}, "props": {}, "effects": {}}

    # These folders hold only what this tool writes.
    for folder in ("Pictures", "Digits", "Faces", "Words"):
        shutil.rmtree(os.path.join(assets, "UI", folder), ignore_errors=True)
    shutil.rmtree(os.path.join(assets, "Fonts"), ignore_errors=True)

    for name, (prefix, letters) in ENEMIES.items():
        ref = "Sprites/Enemies/" + name
        manifest["enemies"][name] = extract_actor(wad, prefix, letters, os.path.join(assets, ref), ref)
        print("enemy", name, manifest["enemies"][name]["w"], manifest["enemies"][name]["h"])

    for group, things in PROPS.items():
        for name, prefix in things.items():
            ref = "Sprites/%s/%s" % (group, name)
            manifest["props"][name] = extract_actor(wad, prefix, None, os.path.join(assets, ref), ref)

    for name, (prefix, letters) in EFFECTS.items():
        ref = "Sprites/Effects/" + name
        manifest["effects"][name] = extract_actor(wad, prefix, letters, os.path.join(assets, ref), ref, centred=True)

    for name, frames in WEAPONS.items():
        extract_weapon(wad, frames, os.path.join(assets, "Sprites", "Weapons", name), WEAPON_LIFT.get(name, 0))

    missing = []
    for name in WALLS:
        if name not in wad.texture_defs():
            missing.append(name)
            continue
        w, h, pixels = wad.texture(name)
        write_png(os.path.join(assets, "Textures", "Walls", name + ".png"), w, h, indices_to_rgba(wad.palette, w, h, pixels))
    for name in SKIES:
        w, h, pixels = wad.texture(name)
        write_png(os.path.join(assets, "Textures", "Sky", name + ".png"), w, h, indices_to_rgba(wad.palette, w, h, pixels))
    for name in FLATS:
        if not wad.has(name):
            missing.append(name)
            continue
        w, h, pixels = wad.flat(name)
        write_png(os.path.join(assets, "Textures", "Flats", name + ".png"), w, h, indices_to_rgba(wad.palette, w, h, pixels))
    for name in UI_PICS:
        if not wad.has(name):
            missing.append(name)
            continue
        w, h, _, _, pixels = wad.picture(name)
        write_png(os.path.join(assets, "UI", "Pictures", name + ".png"), w, h, indices_to_rgba(wad.palette, w, h, pixels))
        manifest.setdefault("pictures", {})[name] = [w, h]

    write_digits(wad, assets)
    write_faces(wad, assets)
    write_bar_side(wad, assets)
    write_tally_digits(assets, manifest)
    write_words(assets, manifest)
    write_font(assets)

    for folder, sounds in SOUNDS.items():
        os.makedirs(os.path.join(assets, "Sounds", folder), exist_ok=True)
        for lump, name in sounds.items():
            if not wad.has(lump) or not wad.sound_to_wav(lump, os.path.join(assets, "Sounds", folder, name + ".wav")):
                missing.append(lump)

    with open(sys.argv[3], "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=1)
    if missing:
        print("MISSING:", " ".join(missing))


if __name__ == "__main__":
    main()
