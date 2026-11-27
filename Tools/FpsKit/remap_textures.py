"""Points levels, prefabs and layouts made with an older kit at the textures'
new folders (Textures/Walls/X.png -> Textures/<kind>/X.png). Only string values
change; every other byte stays as it was.

usage: python -I remap_textures.py <art_manifest.json> <file.bson|.prefab|.uilayout>...
Prints how many paths each file had changed. Back up the project first.
"""
import json
import struct
import sys

OLD = {"wall": "Walls", "flat": "Flats", "sky": "Sky"}


def rewrite(b, i, mapping, count):
    """Re-emits the document at b[i:], with string values mapped. Returns (bytes, end)."""
    n = struct.unpack_from("<i", b, i)[0]
    end = i + n - 1
    j = i + 4
    out = bytearray()
    while j < end:
        t = b[j]
        k = b.index(b"\0", j + 1)
        head = b[j:k + 1]
        j = k + 1
        if t == 2:
            length = struct.unpack_from("<i", b, j)[0]
            value = b[j + 4:j + 4 + length - 1].decode("utf-8")
            j += 4 + length
            if value in mapping:
                value = mapping[value]
                count[0] += 1
            raw = value.encode("utf-8") + b"\0"
            out += head + struct.pack("<i", len(raw)) + raw
        elif t in (3, 4):
            sub, j = rewrite(b, j, mapping, count)
            out += head + sub
        else:
            size = {1: 8, 8: 1, 10: 0, 16: 4, 17: 8, 18: 8, 9: 8, 7: 12}.get(t)
            if t == 5:
                size = 5 + struct.unpack_from("<i", b, j)[0]
            if size is None:
                raise ValueError("BSON type %d not handled" % t)
            out += head + b[j:j + size]
            j += size
    doc = bytes(out) + b"\0"
    return struct.pack("<i", len(doc) + 4) + doc, end + 1


def main():
    manifest = json.load(open(sys.argv[1], encoding="utf-8"))
    mapping = {}
    for kind, folder in OLD.items():
        for name, path in manifest["textures"][kind].items():
            mapping["Textures/%s/%s.png" % (folder, name)] = path
    for path in sys.argv[2:]:
        data = open(path, "rb").read()
        count = [0]
        new, _ = rewrite(data, 0, mapping, count)
        if count[0]:
            open(path, "wb").write(new)
        print(path, count[0])


if __name__ == "__main__":
    main()
