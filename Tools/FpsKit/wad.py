"""Minimal reader for Doom-format WAD files: lumps, the palette, picture and
flat decoding, TEXTURE1/2 compositing and DMX sounds. Standard library only."""
import struct
import wave


class Wad:
    def __init__(self, path):
        with open(path, "rb") as f:
            self.data = f.read()
        magic, count, offset = struct.unpack_from("<4sii", self.data, 0)
        if magic not in (b"IWAD", b"PWAD"):
            raise ValueError("not a WAD: " + path)
        self.lumps = []
        self.index = {}
        for i in range(count):
            pos, size, raw = struct.unpack_from("<ii8s", self.data, offset + i * 16)
            name = raw.split(b"\0")[0].decode("ascii", "replace").upper()
            self.lumps.append((name, pos, size))
            self.index[name] = i  # later lumps win, as in Doom
        self.palette = self._palette()
        self._patch_names = None
        self._textures = None

    def has(self, name):
        return name in self.index

    def lump(self, name):
        _, pos, size = self.lumps[self.index[name]]
        return self.data[pos:pos + size]

    def names_between(self, start, end):
        """Every lump name between two marker lumps."""
        names, inside = [], False
        for name, _, _ in self.lumps:
            if name == start:
                inside = True
            elif name == end:
                inside = False
            elif inside and name:
                names.append(name)
        return names

    def _palette(self):
        raw = self.lump("PLAYPAL")[:768]
        return [(raw[i], raw[i + 1], raw[i + 2]) for i in range(0, 768, 3)]

    def picture(self, name):
        """Decodes a patch to (w, h, leftoffset, topoffset, pixels), pixels being
        a flat list of palette indices with -1 for transparent."""
        return decode_picture(self.lump(name))

    def flat(self, name):
        raw = self.lump(name)
        size = int(len(raw) ** 0.5)
        return size, size, list(raw[:size * size])

    def patch_names(self):
        if self._patch_names is None:
            raw = self.lump("PNAMES")
            count = struct.unpack_from("<i", raw, 0)[0]
            self._patch_names = [raw[4 + i * 8:12 + i * 8].split(b"\0")[0].decode("ascii").upper()
                                 for i in range(count)]
        return self._patch_names

    def texture_defs(self):
        """{name: (w, h, [(xoff, yoff, patch name), ...])}"""
        if self._textures is not None:
            return self._textures
        result = {}
        for lump in ("TEXTURE1", "TEXTURE2"):
            if not self.has(lump):
                continue
            raw = self.lump(lump)
            count = struct.unpack_from("<i", raw, 0)[0]
            offsets = struct.unpack_from("<%di" % count, raw, 4)
            for off in offsets:
                name = raw[off:off + 8].split(b"\0")[0].decode("ascii").upper()
                w, h = struct.unpack_from("<hh", raw, off + 12)
                n = struct.unpack_from("<h", raw, off + 20)[0]
                patches = []
                for i in range(n):
                    ox, oy, pid = struct.unpack_from("<hhh", raw, off + 22 + i * 10)
                    patches.append((ox, oy, self.patch_names()[pid]))
                result[name] = (w, h, patches)
        self._textures = result
        return result

    def texture(self, name):
        """Composites a wall texture to (w, h, pixels)."""
        w, h, patches = self.texture_defs()[name]
        pixels = [-1] * (w * h)
        for ox, oy, patch in patches:
            pw, ph, _, _, src = self.picture(patch)
            for y in range(ph):
                ty = y + oy
                if ty < 0 or ty >= h:
                    continue
                for x in range(pw):
                    tx = x + ox
                    if tx < 0 or tx >= w:
                        continue
                    c = src[y * pw + x]
                    if c >= 0:
                        pixels[ty * w + tx] = c
        return w, h, pixels

    def sound_to_wav(self, name, path):
        """DMX sound lump to an 8-bit mono WAV. False if it isn't one."""
        raw = self.lump(name)
        if len(raw) < 8:
            return False
        fmt, rate, count = struct.unpack_from("<HHI", raw, 0)
        if fmt != 3:
            return False
        samples = raw[8:8 + count]
        # DMX pads 16 bytes on each side.
        if len(samples) > 32:
            samples = samples[16:-16]
        with wave.open(path, "wb") as w:
            w.setnchannels(1)
            w.setsampwidth(1)
            w.setframerate(rate)
            w.writeframes(samples)
        return True


def decode_picture(raw):
    w, h, left, top = struct.unpack_from("<hhhh", raw, 0)
    offsets = struct.unpack_from("<%di" % w, raw, 8)
    pixels = [-1] * (w * h)
    for x in range(w):
        p = offsets[x]
        while raw[p] != 0xFF:
            row = raw[p]
            n = raw[p + 1]
            for i in range(n):
                y = row + i
                if 0 <= y < h:
                    pixels[y * w + x] = raw[p + 3 + i]
            p += n + 4
    return w, h, left, top, pixels
