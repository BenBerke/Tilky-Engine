"""Writes a pixel font as a TrueType file: every lit pixel run becomes a square
outline. Just the tables FreeType needs, standard library only."""
import os
import struct

UNITS_PER_PIXEL = 64


def _rects(rows):
    """Merges lit pixels into rectangles (x, y, w, h), y going down."""
    rects, open_runs = [], {}
    for y, row in enumerate(rows):
        runs, x = [], 0
        while x < len(row):
            if row[x]:
                start = x
                while x < len(row) and row[x]:
                    x += 1
                runs.append((start, x - start))
            else:
                x += 1
        next_open = {}
        for run in runs:
            if run in open_runs:
                next_open[run] = open_runs.pop(run)
                next_open[run][3] += 1
            else:
                next_open[run] = [run[0], y, run[1], 1]
        rects.extend(open_runs.values())
        open_runs = next_open
    rects.extend(open_runs.values())
    return rects


def _glyph(rows, ascent_px):
    if not any(any(r) for r in rows):
        return b"", (0, 0, 0, 0)
    pts, ends = [], []
    for x, y, w, h in _rects(rows):
        # Clockwise, TrueType's filled direction. y flips: font y goes up.
        top = (ascent_px - y) * UNITS_PER_PIXEL
        bottom = (ascent_px - y - h) * UNITS_PER_PIXEL
        left = x * UNITS_PER_PIXEL
        right = (x + w) * UNITS_PER_PIXEL
        pts += [(left, top), (right, top), (right, bottom), (left, bottom)]
        ends.append(len(pts) - 1)
    xs, ys = [p[0] for p in pts], [p[1] for p in pts]
    box = (min(xs), min(ys), max(xs), max(ys))
    out = struct.pack(">hhhhh", len(ends), *box)
    out += b"".join(struct.pack(">H", e) for e in ends)
    out += struct.pack(">H", 0)  # no instructions
    out += bytes([0x01] * len(pts))  # all on-curve, 16-bit deltas
    px = py = 0
    xd = yd = b""
    for x, y in pts:
        xd += struct.pack(">h", x - px)
        yd += struct.pack(">h", y - py)
        px, py = x, y
    out += xd + yd
    if len(out) % 2:
        out += b"\0"
    return out, box


def _name_table(family):
    records = [(1, family), (2, "Regular"), (3, family), (4, family), (5, "Version 1.0"), (6, family.replace(" ", ""))]
    strings, entries, offset = b"", b"", 0
    for name_id, text in records:
        data = text.encode("utf-16-be")
        entries += struct.pack(">HHHHHH", 3, 1, 0x409, name_id, len(data), offset)
        strings += data
        offset += len(data)
    return struct.pack(">HHH", 0, len(records), 6 + len(entries)) + entries + strings


def write_pixel_font(path, family, glyphs, height, descent):
    """glyphs: {char: (advance width in pixels, rows of booleans)}. `height` is
    the font's pixel height, `descent` how many pixels sit below the baseline."""
    ascent_px = height - descent
    chars = sorted(glyphs)
    glyf, loca, hmtx = b"", [0], b""
    # Glyph 0: empty .notdef.
    loca.append(0)
    hmtx += struct.pack(">Hh", 4 * UNITS_PER_PIXEL, 0)
    box_all = [0, 0, 0, 0]
    for c in chars:
        advance, rows = glyphs[c]
        data, box = _glyph(rows, ascent_px)
        glyf += data
        loca.append(len(glyf))
        # One pixel of spacing after every glyph.
        hmtx += struct.pack(">Hh", (advance + 1) * UNITS_PER_PIXEL, box[0])
        box_all = [min(box_all[0], box[0]), min(box_all[1], box[1]), max(box_all[2], box[2]), max(box_all[3], box[3])]

    em = height * UNITS_PER_PIXEL
    advance_max = max(g[0] + 1 for g in glyphs.values()) * UNITS_PER_PIXEL
    ascent, desc = ascent_px * UNITS_PER_PIXEL, -descent * UNITS_PER_PIXEL

    head = struct.pack(">HHiiIHHqqhhhhHHhhh", 1, 0, 0x00010000, 0, 0x5F0F3CF5, 0x000B, em, 0, 0,
                       box_all[0], box_all[1], box_all[2], box_all[3], 0, 8, 2, 1, 0)
    hhea = struct.pack(">HHhhhHhhhhhhhhhhhH", 1, 0, ascent, desc, 0, advance_max, 0, 0, advance_max, 1, 0, 0,
                       0, 0, 0, 0, 0, len(chars) + 1)
    maxp = struct.pack(">HHHHHHHHHHHHHHHH", 1, 0, len(chars) + 1, 4096, 512, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0)
    os2 = struct.pack(">HhHHHhhhhhhhhhhh", 3, advance_max // 2, 400, 5, 0, em // 2, em // 2, 0, 0, em // 2, em // 2,
                      0, em // 2, 50, 200, 0)
    os2 += bytes(10) + bytes(16) + b"TILK" + struct.pack(">HHH", 0x40, 32, 255)
    os2 += struct.pack(">hhhHH", ascent, desc, 0, ascent, -desc) + struct.pack(">II", 1, 0)
    os2 += struct.pack(">hhHHH", ascent // 2, em // 2, 0, 32, 0)
    post = struct.pack(">IiHhIIIII", 0x00030000, 0, -UNITS_PER_PIXEL & 0xFFFF, UNITS_PER_PIXEL, 0, 0, 0, 0, 0)

    # cmap: format 4, one segment per character, then the required 0xFFFF one.
    seg = len(chars) + 1
    codes = [ord(c) for c in chars] + [0xFFFF]
    deltas = [(i + 1 - codes[i]) & 0xFFFF for i in range(len(chars))] + [1]
    search = 2
    entry = 0
    while search * 2 <= seg * 2:
        search *= 2
        entry += 1
    body = struct.pack(">HHHH", seg * 2, search, entry, seg * 2 - search)
    body += b"".join(struct.pack(">H", c) for c in codes) + struct.pack(">H", 0)
    body += b"".join(struct.pack(">H", c) for c in codes)
    body += b"".join(struct.pack(">H", d) for d in deltas)
    body += b"\0\0" * seg
    sub = struct.pack(">HHH", 4, 6 + len(body), 0) + body
    cmap = struct.pack(">HH", 0, 1) + struct.pack(">HHI", 3, 1, 12) + sub

    tables = {
        b"OS/2": os2, b"cmap": cmap, b"glyf": glyf, b"head": head, b"hhea": hhea, b"hmtx": hmtx,
        b"loca": b"".join(struct.pack(">I", o) for o in loca), b"maxp": maxp, b"name": _name_table(family), b"post": post,
    }
    count = len(tables)
    power = 1
    log = 0
    while power * 2 <= count:
        power *= 2
        log += 1
    out = struct.pack(">IHHHH", 0x00010000, count, power * 16, log, count * 16 - power * 16)
    offset = 12 + count * 16
    directory, data = b"", b""
    for tag in sorted(tables):
        raw = tables[tag]
        padded = raw + b"\0" * (-len(raw) % 4)
        total = 0
        for i in range(0, len(padded), 4):
            total = (total + struct.unpack_from(">I", padded, i)[0]) & 0xFFFFFFFF
        directory += tag + struct.pack(">III", total, offset, len(raw))
        data += padded
        offset += len(padded)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(out + directory + data)
