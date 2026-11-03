"""Draws the Classic FPS Kit's effect art and sounds with code: blood, stains, chunks,
casings, scorch marks, the grenade, the kick and throw pictures, screen blood, and
the Options menu's words. Freedoom has none of these. All of it is original to this
tool, so it carries no licence terms beyond the repo's. Same seed, same pictures.

usage: python fx_art.py <Assets folder> <art_manifest.json>
Run it after extract.py, which rewrites the manifest. Needs Pillow.
Writes to Assets/Sprites/FX, Assets/UI/Actions, Assets/UI/Words and Assets/Sounds/FX.
"""
import json
import math
import os
import random
import struct
import sys
import wave

from PIL import Image, ImageDraw, ImageFilter

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from letters import draw_word  # noqa: E402

rng = random.Random(1993)

# The status bar's own browns and Doom's reds and steels, so it sits with Freedoom's art.
BLOOD = [(120, 0, 0), (160, 8, 8), (196, 20, 16)]
BLOOD_DARK = [(70, 0, 0), (96, 0, 0), (120, 4, 4)]
FLESH = [(150, 70, 60), (120, 40, 36), (176, 96, 80), (90, 20, 20)]
STEEL = [(40, 44, 40), (64, 70, 60), (92, 98, 84), (124, 130, 112)]
BOOT = [(26, 22, 18), (44, 36, 28), (66, 54, 40), (92, 76, 56), (120, 102, 76)]
SKIN = [(92, 52, 36), (132, 80, 56), (168, 112, 80), (196, 144, 108)]
ARMOUR = [(28, 44, 20), (44, 64, 28), (64, 88, 40), (88, 112, 56)]


def save(img, assets, *parts):
    path = os.path.join(assets, *parts)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    img.save(path)


def blob(size, color_ramp, lumps=5, spread=0.3):
    """A rough little lump, chunky pixels, hard edge."""
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    centre = (size - 1) / 2
    for _ in range(lumps):
        r = rng.uniform(size * 0.14, size * 0.3)
        x = centre + rng.uniform(-size * spread, size * spread)
        y = centre + rng.uniform(-size * spread, size * spread)
        d.ellipse((x - r, y - r, x + r, y + r), fill=rng.choice(color_ramp) + (255,))
    # A lighter speck, so it catches the eye as it flies.
    x, y = centre + rng.uniform(-1, 1), centre + rng.uniform(-1, 1)
    d.point((x, y), fill=color_ramp[-1] + (255,))
    return img


def splat(size, ramp, droplets=26, reach=0.48, core=0.2):
    """A top-down splash: a torn core with streaks and flecks flung outward."""
    scale = 3
    big = size * scale
    mask = Image.new("L", (big, big), 0)
    d = ImageDraw.Draw(mask)
    c = big / 2
    for _ in range(9):
        r = rng.uniform(core * 0.5, core) * big
        a = rng.uniform(0, math.tau)
        o = rng.uniform(0, core * 0.8) * big
        x, y = c + math.cos(a) * o, c + math.sin(a) * o
        d.ellipse((x - r, y - r, x + r, y + r), fill=255)
    for _ in range(droplets):
        a = rng.uniform(0, math.tau)
        far = rng.uniform(core, reach) * big
        x, y = c + math.cos(a) * far, c + math.sin(a) * far
        d.line((c + math.cos(a) * far * 0.5, c + math.sin(a) * far * 0.5, x, y), fill=255, width=max(1, int(scale * 0.7)))
        r = rng.uniform(0.012, 0.04) * big
        d.ellipse((x - r, y - r, x + r, y + r), fill=255)
    mask = mask.filter(ImageFilter.GaussianBlur(scale * 0.5)).resize((size, size), Image.BILINEAR)

    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    px, m = img.load(), mask.load()
    for y in range(size):
        for x in range(size):
            if m[x, y] > 110:
                shade = ramp[min(len(ramp) - 1, int((m[x, y] - 110) / 146 * len(ramp) + rng.uniform(-0.4, 0.4)))]
                px[x, y] = shade + (235,)
    return img


def scorch(size):
    """Burnt ground: dark in the middle, broken and see-through at the edge."""
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    px = img.load()
    c = (size - 1) / 2
    bumps = [(rng.uniform(0, math.tau), rng.uniform(0.015, 0.05), rng.randint(3, 11)) for _ in range(6)]
    for y in range(size):
        for x in range(size):
            dx, dy = x - c, y - c
            r = math.hypot(dx, dy) / c
            a = math.atan2(dy, dx)
            edge = 0.88 + sum(b * math.sin(n * a + p) for p, b, n in bumps)
            t = r / edge
            if t < 1 and rng.random() > (t ** 3) * 0.6:
                v = int(6 + 26 * t + rng.uniform(-4, 6))
                px[x, y] = (v, int(v * 0.9), int(v * 0.8), int(245 * (1 - t ** 2.2)) + 10)
    return img


def bullet_hole(size):
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    c = (size - 1) / 2
    r = size * 0.42
    d.ellipse((c - r, c - r, c + r, c + r), fill=(52, 50, 46, 200))
    r = size * 0.26
    d.ellipse((c - r, c - r, c + r, c + r), fill=(8, 8, 8, 255))
    return img


def ring(size, color):
    """A ripple seen from above."""
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    c = (size - 1) / 2
    r = size * 0.44
    d.ellipse((c - r, c - r, c + r, c + r), outline=color + (210,), width=max(1, size // 14))
    r = size * 0.28
    d.ellipse((c - r, c - r, c + r, c + r), outline=color + (120,), width=1)
    return img


def smoke(size, frame, frames):
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    c = size / 2
    for _ in range(8):
        r = rng.uniform(size * 0.14, size * 0.26) * (1 + frame / frames * 0.4)
        x, y = c + rng.uniform(-size * 0.2, size * 0.2), c + rng.uniform(-size * 0.2, size * 0.2)
        v = rng.randint(70, 120)
        d.ellipse((x - r, y - r, x + r, y + r), fill=(v, v, v, 170))
    return img.filter(ImageFilter.GaussianBlur(1))


def casing(w, h, body, head):
    img = Image.new("RGBA", (w + 2, w + 2), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    y = (w + 2 - h) // 2
    d.rectangle((1, y, w, y + h - 1), fill=body + (255,))
    d.rectangle((1, y, 2, y + h - 1), fill=head + (255,))
    return img


def grenade(frame):
    """A dull steel egg with a spoon. Four turns, for spinning as it flies."""
    size = 16
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.ellipse((3, 4, 12, 14), fill=STEEL[1] + (255,), outline=STEEL[0] + (255,))
    for x in (5, 7, 9):
        d.line((x, 5, x, 13), fill=STEEL[0] + (255,))
    for y in (7, 10):
        d.line((4, y, 11, y), fill=STEEL[0] + (255,))
    d.point([(5, 6), (6, 6), (5, 8)], fill=STEEL[3] + (255,))
    d.rectangle((6, 1, 9, 4), fill=STEEL[2] + (255,), outline=STEEL[0] + (255,))
    d.line((9, 2, 13, 5), fill=(176, 150, 60, 255))
    return img.rotate(-90 * frame, resample=Image.NEAREST, center=(size / 2 - 0.5, size / 2 - 0.5))


def grenade_box():
    img = Image.new("RGBA", (26, 20), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.rectangle((0, 7, 25, 19), fill=ARMOUR[1] + (255,), outline=ARMOUR[0] + (255,))
    d.rectangle((1, 8, 24, 10), fill=ARMOUR[3] + (255,))
    for x in (6, 13, 20):
        d.line((x, 11, x, 18), fill=ARMOUR[0] + (255,))
    for x in (2, 10, 17):
        img.alpha_composite(grenade(0).crop((2, 0, 14, 12)).resize((8, 8), Image.NEAREST), (x, 0))
    return img


def shade_polygon(img, points, ramp, light=(-0.6, -0.8)):
    """Fills a polygon, shading each pixel across the shape toward `light`, with
    Doom-like ordered dither so that it reads as painted pixels, not a gradient."""
    mask = Image.new("L", img.size, 0)
    ImageDraw.Draw(mask).polygon(points, fill=255)
    box = mask.getbbox()
    if box is None:
        return
    px, m = img.load(), mask.load()
    x0, y0, x1, y1 = box
    bayer = [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]
    for y in range(y0, y1):
        for x in range(x0, x1):
            if not m[x, y]:
                continue
            u = (x - x0) / max(1, x1 - x0) - 0.5
            v = (y - y0) / max(1, y1 - y0) - 0.5
            t = 0.5 + (u * light[0] + v * light[1])
            t = min(0.999, max(0, t + (bayer[y % 4][x % 4] / 16 - 0.5) * 0.28))
            px[x, y] = ramp[int(t * len(ramp))] + (255,)


def boot(img, foot, lift):
    """The player's leg, thigh to boot, coming up from the bottom of a 320 x 168
    picture. `foot` is the toe's centre, `lift` 0 to 1 how far the leg is out: more
    lift, more of the sole is seen and the bigger it gets."""
    fx, fy = foot
    s = 0.7 + lift * 0.75
    knee = (fx + 30 * s, fy + 66 * s)
    hip = (fx + 56, 190)

    # Trouser leg.
    shade_polygon(img, [(hip[0] - 48 * s, 190), (knee[0] - 30 * s, knee[1]), (knee[0] + 26 * s, knee[1] - 6 * s), (hip[0] + 52 * s, 190)], ARMOUR)
    # Shin.
    shade_polygon(img, [(knee[0] - 26 * s, knee[1] + 10), (fx - 16 * s, fy + 22 * s), (fx + 22 * s, fy + 14 * s), (knee[0] + 24 * s, knee[1] - 2)], ARMOUR)
    # Boot shaft.
    shade_polygon(img, [(fx - 22 * s, fy + 28 * s), (fx - 24 * s, fy + 2 * s), (fx + 26 * s, fy - 6 * s), (fx + 28 * s, fy + 22 * s)], BOOT)
    # The sole, facing us: the foot pointing at whatever it's about to hit.
    sole = [(fx - 30 * s, fy + 4 * s), (fx - 34 * s, fy - 26 * s), (fx - 18 * s, fy - 46 * s), (fx + 14 * s, fy - 50 * s),
            (fx + 34 * s, fy - 32 * s), (fx + 32 * s, fy - 2 * s), (fx + 10 * s, fy + 8 * s)]
    shade_polygon(img, sole, BOOT)

    d = ImageDraw.Draw(img)
    d.line(sole + [sole[0]], fill=BOOT[0] + (255,), width=max(1, int(2 * s)))
    # Heel plate and treads.
    d.line((fx - 24 * s, fy - 10 * s, fx + 24 * s, fy - 12 * s), fill=BOOT[0] + (255,), width=max(1, int(3 * s)))
    for i in range(4):
        y = fy - 20 * s - i * 6.5 * s
        half = (26 - abs(i - 1) * 3) * s
        d.line((fx - half, y, fx + half, y - 1), fill=BOOT[0] + (255,), width=max(1, int(1.5 * s)))
    for i in range(2):
        y = fy + (-3 - i * 4) * s
        d.line((fx - 16 * s, y, fx + 14 * s, y), fill=BOOT[4] + (255,))


def kick_frames():
    frames = []
    # Up from below, out, and back down.
    for lift, foot in [(0.05, (178, 160)), (0.35, (172, 128)), (0.72, (166, 96)), (1.0, (160, 78)), (0.6, (168, 108)), (0.2, (176, 146))]:
        img = Image.new("RGBA", (320, 168), (0, 0, 0, 0))
        boot(img, foot, lift)
        frames.append(img)
    return frames


def forearm(img, hand, spread):
    """A bare forearm coming in from the bottom right, ending in a hand at `hand`."""
    hx, hy = hand
    shade_polygon(img, [(hx + 2, hy + 16), (hx + 30, hy + 2), (316, 150), (316, 190), (262, 190)], SKIN)
    # Glove: a fist, its fingers opening as it lets go.
    shade_polygon(img, [(hx - 12, hy + 10), (hx - 18, hy - 8), (hx + 8, hy - 18), (hx + 30, hy - 6), (hx + 32, hy + 12), (hx + 6, hy + 24)], BOOT)
    d = ImageDraw.Draw(img)
    for i in range(4):
        x = hx - 12 + i * 8
        d.line((x, hy - 8 - i, x - spread * 2, hy - 18 - i - spread * 3), fill=BOOT[2] + (255,), width=4)
        d.line((x + 1, hy - 8 - i, x - spread * 2 + 1, hy - 18 - i - spread * 3), fill=BOOT[3] + (255,), width=1)


def throw_frames():
    frames = []
    # Hand up with the grenade, forward, then open and empty.
    for hand, spread, holding in [((236, 150), 0, True), ((214, 104), 0, True), ((186, 70), 1, True), ((170, 62), 3, False), ((206, 104), 2, False), ((244, 156), 1, False)]:
        img = Image.new("RGBA", (320, 168), (0, 0, 0, 0))
        forearm(img, hand, spread)
        if holding:
            g = grenade(0).resize((48, 48), Image.NEAREST)
            img.alpha_composite(g, (hand[0] - 28, hand[1] - 56))
            # The fingers go in front of it.
            forearm(img, hand, spread)
        frames.append(img)
    return frames


def screen_blood(index):
    """Blood running down the inside of the visor: heavy at the edges, clear in the middle."""
    w, h = 480, 300
    mask = Image.new("L", (w, h), 0)
    d = ImageDraw.Draw(mask)
    edges = [(0, 0), (w, 0), (0, h), (w, h), (w * 0.5, 0), (0, h * 0.5), (w, h * 0.5)]
    picks = rng.sample(edges, 3 + index % 2)
    for ex, ey in picks:
        for _ in range(26):
            r = rng.uniform(6, 34)
            x = ex + rng.gauss(0, w * 0.1)
            y = ey + rng.gauss(0, h * 0.13)
            d.ellipse((x - r, y - r, x + r, y + r), fill=255)
        for _ in range(14):
            x = ex + rng.gauss(0, w * 0.15)
            y0 = ey + rng.uniform(-10, h * 0.16)
            length = rng.uniform(20, 120)
            width = rng.randint(2, 7)
            d.line((x, y0, x + rng.uniform(-3, 3), y0 + length), fill=255, width=width)
            d.ellipse((x - width, y0 + length - width, x + width, y0 + length + width), fill=255)
        for _ in range(30):
            a, far = rng.uniform(0, math.tau), rng.uniform(40, 170)
            x, y = ex + math.cos(a) * far, ey + math.sin(a) * far
            r = rng.uniform(1, 5)
            d.ellipse((x - r, y - r, x + r, y + r), fill=255)
    mask = mask.filter(ImageFilter.GaussianBlur(1.6)).point(lambda v: 255 if v > 90 else 0)

    img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    px, m = img.load(), mask.load()
    for y in range(h):
        for x in range(w):
            if m[x, y]:
                near_edge = m[x, max(0, y - 2)] == 0 or m[max(0, x - 2), y] == 0
                base = (176, 16, 12) if near_edge else (118 + rng.randint(-8, 8), 2, 2)
                px[x, y] = base + (255,)
    return img


# ── Sounds: 11025 Hz, 8-bit mono, as Doom's are ─────────────────────────────

RATE = 11025


def write_sound(assets, name, samples):
    path = os.path.join(assets, "Sounds", "FX", name + ".wav")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    peak = max(1e-6, max(abs(s) for s in samples))
    with wave.open(path, "wb") as f:
        f.setnchannels(1)
        f.setsampwidth(1)
        f.setframerate(RATE)
        f.writeframes(bytes(max(0, min(255, int(128 + s / peak * 120))) for s in samples))


def noise_burst(seconds, cutoff_from, cutoff_to, attack=0.005, decay=4.0):
    out, low = [], 0.0
    n = int(seconds * RATE)
    for i in range(n):
        t = i / n
        cutoff = cutoff_from + (cutoff_to - cutoff_from) * t
        a = min(1.0, 2 * math.pi * cutoff / RATE)
        low += (rng.uniform(-1, 1) - low) * a
        env = min(1.0, (i / RATE) / attack) * math.exp(-decay * t)
        out.append(low * env)
    return out


def thump(seconds, freq_from, freq_to, decay=6.0):
    out, phase = [], 0.0
    n = int(seconds * RATE)
    for i in range(n):
        t = i / n
        phase += 2 * math.pi * (freq_from + (freq_to - freq_from) * t) / RATE
        out.append(math.sin(phase) * math.exp(-decay * t))
    return out


def clink(seconds, freqs, decay=9.0):
    n = int(seconds * RATE)
    return [sum(math.sin(2 * math.pi * f * i / RATE) for f in freqs) * math.exp(-decay * i / n) for i in range(n)]


def mix(*tracks):
    out = [0.0] * max(len(t) for t in tracks)
    for track in tracks:
        for i, s in enumerate(track):
            out[i] += s
    return out


def write_sounds(assets):
    write_sound(assets, "kick swing", noise_burst(0.22, 300, 1800, attack=0.06, decay=3.5))
    write_sound(assets, "kick hit", mix(thump(0.16, 170, 60), [s * 0.5 for s in noise_burst(0.08, 2000, 500)]))
    write_sound(assets, "grenade throw", noise_burst(0.26, 800, 2400, attack=0.08, decay=3.0))
    write_sound(assets, "grenade pin", clink(0.09, [2400, 3300], 7.0))
    write_sound(assets, "grenade bounce", mix(clink(0.14, [980, 1610, 2320]), [s * 0.5 for s in thump(0.08, 260, 120)]))
    write_sound(assets, "shell drop", clink(0.09, [2800, 4300], 8.0))
    write_sound(assets, "splash water", mix(noise_burst(0.34, 3000, 500, attack=0.004, decay=3.6), [s * 0.5 for s in thump(0.14, 240, 90)]))
    write_sound(assets, "splash goo", mix(noise_burst(0.3, 1200, 240, decay=3.5), [s * 0.8 for s in thump(0.22, 150, 50, 4.0)]))
    write_sound(assets, "splash lava", mix([s * 0.6 for s in thump(0.2, 110, 45, 4.0)], noise_burst(0.5, 4000, 2000, attack=0.03, decay=3.0)))
    write_sound(assets, "blood hit", mix(noise_burst(0.1, 1400, 400, decay=4.0), [s * 0.6 for s in thump(0.08, 180, 80)]))


def main():
    assets, manifest_path = sys.argv[1], sys.argv[2]

    for i in range(4):
        save(blob(8, BLOOD), assets, "Sprites", "FX", "Blood", f"Drop {i + 1}.png")
    for i in range(5):
        save(splat(48, BLOOD_DARK), assets, "Sprites", "FX", "Blood", f"Stain {i + 1}.png")
    for i in range(3):
        save(splat(32, BLOOD_DARK, droplets=12, reach=0.46, core=0.13), assets, "Sprites", "FX", "Blood", f"Small Stain {i + 1}.png")
    for i in range(6):
        save(blob(16, FLESH, lumps=7, spread=0.24), assets, "Sprites", "FX", "Gibs", f"Chunk {i + 1}.png")

    for i in range(3):
        save(scorch(64), assets, "Sprites", "FX", "Marks", f"Scorch {i + 1}.png")
    save(bullet_hole(8), assets, "Sprites", "FX", "Marks", "Bullet Hole.png")

    for i in range(3):
        save(blob(8, STEEL), assets, "Sprites", "FX", "Debris", f"Rock {i + 1}.png")
    for i in range(3):
        save(smoke(24, i, 3), assets, "Sprites", "FX", "Debris", f"Smoke {i + 1}.png")
    save(blob(4, [(255, 200, 80), (255, 150, 30)], lumps=2, spread=0.1), assets, "Sprites", "FX", "Debris", "Spark.png")
    save(casing(6, 3, (196, 160, 60), (240, 214, 120)), assets, "Sprites", "FX", "Casings", "Bullet.png")
    save(casing(9, 4, (150, 24, 20), (196, 160, 60)), assets, "Sprites", "FX", "Casings", "Shell.png")

    liquids = {"Water": ([(60, 90, 170), (96, 130, 210), (170, 200, 240)], (170, 200, 240)),
               "Nukage": ([(40, 130, 20), (80, 190, 30), (170, 240, 70)], (140, 230, 60)),
               "Lava": ([(190, 50, 0), (240, 110, 0), (255, 200, 50)], (255, 170, 40))}
    for name, (ramp, ripple) in liquids.items():
        for i in range(3):
            save(blob(8, ramp), assets, "Sprites", "FX", "Splash", f"{name} Drop {i + 1}.png")
        save(ring(32, ripple), assets, "Sprites", "FX", "Splash", f"{name} Ripple.png")

    for i in range(4):
        save(grenade(i), assets, "Sprites", "FX", "Grenade", f"Spin {i + 1}.png")
    save(grenade_box(), assets, "Sprites", "FX", "Grenade", "Box.png")
    save(grenade(0).crop((1, 0, 15, 16)), assets, "UI", "Actions", "Grenade Icon.png")

    for i, frame in enumerate(kick_frames()):
        save(frame, assets, "UI", "Actions", f"Kick {i + 1}.png")
    for i, frame in enumerate(throw_frames()):
        save(frame, assets, "UI", "Actions", f"Throw {i + 1}.png")
    for i in range(4):
        save(screen_blood(i), assets, "UI", "Actions", f"Screen Blood {i + 1}.png")

    write_sounds(assets)

    with open(manifest_path, encoding="utf-8") as f:
        manifest = json.load(f)
    for word in ["Options", "Gore", "Off", "Normal", "Extra"]:
        w, h, rgba = draw_word(word)
        path = os.path.join(assets, "UI", "Words", word + ".png")
        Image.frombytes("RGBA", (w, h), bytes(rgba)).save(path)
        manifest.setdefault("words", {})[word] = [w, h]
    manifest["props"]["Grenade"] = {"w": 16, "h": 16, "path": "Sprites/FX/Grenade", "frames": {"Spin 1": False}}
    manifest["props"]["Grenade Box"] = {"w": 26, "h": 20, "path": "Sprites/FX/Grenade", "frames": {"Box": False}}
    with open(manifest_path, "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=1)
    print("OK")


if __name__ == "__main__":
    main()
