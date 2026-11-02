"""Downloads the Classic FPS Kit's music into the template and writes its credits.

usage: python -I music.py <work folder> <template Assets folder>

Every track is CC0 on OpenGameArt: check a track's own page before adding it here.
Needs ffmpeg on the PATH, and 7-Zip for the one track that comes in a .7z.

Tracks are decoded into memory when the project loads, so keep them short or low
quality. `trim` cuts silence from the end, which would otherwise break the loop.
"""
import os, re, shutil, subprocess, sys, urllib.parse, urllib.request

BASE = "https://opengameart.org/sites/default/files/"
TRACKS = [
    {"name": "Post Apocalyptic Wastelands", "author": "Juhani Junkala (SubspaceAudio)",
     "page": "https://opengameart.org/content/horror-atmosphere",
     "file": "Juhani Junkala - Post Apocalyptic Wastelands [Loop Ready].ogg", "use": "Main Menu", "quality": 2},
    {"name": "Rocky Musicloop", "author": "johndekale",
     "page": "https://opengameart.org/content/rocky-musicloop",
     "file": "loop.7z", "inside": "loop/loop.ogg", "use": "Level 1, Blank Base Level"},
    {"name": "Heavy Battle 2", "author": "MintoDog",
     "page": "https://opengameart.org/node/183145",
     "file": "heavy_battle_2_bpm185_0.ogg", "use": "Level 2"},
    {"name": "Swordfight III", "author": "Kistol",
     "page": "https://opengameart.org/content/swordfight-iii",
     "file": "Swordfight III_4.ogg", "use": "Level 3", "trim": True},
    {"name": "Boss Battle 10", "author": "nene",
     "page": "https://opengameart.org/content/boss-battle-10-metal",
     "file": "boss_battle_10_metal.wav", "use": "Level 4", "trim": True},
]


def seven_zip():
    found = shutil.which("7z") or r"C:\Program Files\7-Zip\7z.exe"
    if not os.path.exists(found): sys.exit("music.py needs 7-Zip")
    return found


def music_end(path):
    """Where the music stops, if the file ends in silence; otherwise None."""
    log = subprocess.run(["ffmpeg", "-hide_banner", "-nostdin", "-i", path, "-af", "silencedetect=n=-55dB:d=0.3",
                          "-f", "null", "-"], capture_output=True, text=True).stderr
    starts = re.findall(r"silence_start: ([\d.]+)", log)
    ends = re.findall(r"silence_end: ([\d.]+)", log)
    # Silence that runs to the end of the file has no silence_end before the last start, or ends at the duration.
    hours, minutes, seconds = re.search(r"Duration: (\d+):(\d+):([\d.]+)", log).groups()
    duration = int(hours) * 3600 + int(minutes) * 60 + float(seconds)
    if not starts: return None
    if len(ends) >= len(starts) and abs(float(ends[-1]) - duration) > 0.1: return None
    return float(starts[-1]) + 0.1


def main():
    work, assets = sys.argv[1], sys.argv[2]
    music = os.path.join(assets, "Music")
    os.makedirs(work, exist_ok=True)
    os.makedirs(music, exist_ok=True)

    credits = ["Music in this project. Every track is CC0 (public domain dedication): you can use, change and sell",
               "it with no credit required, though crediting the composers is a kind thing to do.", ""]
    for track in TRACKS:
        download = os.path.join(work, track["file"])
        if not os.path.exists(download):
            urllib.request.urlretrieve(BASE + urllib.parse.quote(track["file"]), download)
        source = download
        if "inside" in track:
            unpacked = os.path.join(work, track["name"])
            subprocess.run([seven_zip(), "x", "-y", "-o" + unpacked, download], check=True, capture_output=True)
            source = os.path.join(unpacked, *track["inside"].split("/"))

        out = os.path.join(music, track["name"] + ".ogg")
        end = music_end(source) if track.get("trim") else None
        if source.endswith(".ogg") and end is None and "quality" not in track:
            shutil.copyfile(source, out)  # already fine: no second round of lossy encoding
        else:
            command = ["ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-nostdin", "-i", source]
            if end is not None: command += ["-t", f"{end:.3f}"]
            command += ["-vn", "-map_metadata", "-1", "-c:a", "libvorbis", "-q:a", str(track.get("quality", 6)), out]
            subprocess.run(command, check=True)
        print(f"{track['name']}: {os.path.getsize(out) // 1024} KB" + (f", cut at {end:.1f}s" if end else ""))

        credits += [track["name"], f"  By: {track['author']}", f"  From: {track['page']}", "  Licence: CC0",
                    f"  Used in: {track['use']}", ""]

    with open(os.path.join(music, "CREDITS.txt"), "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(credits))


main()
