#!/usr/bin/env python3
"""Standard test assets for rendering checks (queue 3-1b).

The models are not stored in the repository (their licences differ: CC0, CC-BY-4.0). This tool downloads them into
.cache/testassets/ and checks them against Tests/testassets.lock.json (pinned commit, size, SHA-256, licence and
credit for every file).

  python tools/testassets.py fetch            download what is missing or changed (then verify)
  python tools/testassets.py verify           check the downloaded files against the lock file
  python tools/testassets.py credits          print the attribution lines (CC-BY needs them)
  python tools/testassets.py lock             maintainers: rebuild the lock file from the pinned commit

Only the Python standard library is used.
"""
import hashlib
import json
import os
import sys
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LOCK = os.path.join(ROOT, "Tests", "testassets.lock.json")
DEST = os.path.join(ROOT, ".cache", "testassets")
REPO = "KhronosGroup/glTF-Sample-Assets"
COMMIT = "e124e8d8d7330a5372bc2b4d00d39a4a9d7997ea"
# What each model checks on the reference board.
MODELS = {
    "MetalRoughSpheres": "metal/rough texture, PBR spheres against the procedural matrix",
    "NormalTangentMirrorTest": "normal maps with tangents and mirrored UVs",
    "TextureCoordinateTest": "UV origin and direction",
    "AlphaBlendModeTest": "OPAQUE / MASK / BLEND and alpha cutoff",
}
AGENT = {"User-Agent": "bidulgi-testassets"}


def raw_url(path):
    return f"https://raw.githubusercontent.com/{REPO}/{COMMIT}/{path}"


def get(url):
    with urllib.request.urlopen(urllib.request.Request(url, headers=AGENT), timeout=120) as r:
        return r.read()


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def load_lock():
    with open(LOCK, encoding="utf-8") as f:
        return json.load(f)


def cmd_lock():
    files = []
    for name, purpose in MODELS.items():
        meta = json.loads(get(raw_url(f"Models/{name}/metadata.json")))
        legal = meta["legal"][0]
        for kind, path in (("model", f"Models/{name}/glTF-Binary/{name}.glb"),
                           ("screenshot", f"Models/{name}/{meta.get('screenshot', 'screenshot/screenshot.png')}")):
            data = get(raw_url(path))
            files.append({
                "name": name, "kind": kind, "file": f"{name}.glb" if kind == "model" else f"{name}.screenshot.png",
                "url": raw_url(path), "bytes": len(data), "sha256": sha256(data), "purpose": purpose,
                "license": legal["spdx"], "artist": legal.get("artist", ""), "owner": legal.get("owner", ""),
                "year": legal.get("year", ""),
            })
            print(f"locked {files[-1]['file']} ({len(data)} bytes, {legal['spdx']})")
    lock = {"source": f"https://github.com/{REPO}", "commit": COMMIT, "files": files}
    with open(LOCK, "w", encoding="utf-8", newline="\n") as f:
        json.dump(lock, f, indent=2)
        f.write("\n")


def check(entry):
    path = os.path.join(DEST, entry["file"])
    if not os.path.exists(path):
        return "missing"
    with open(path, "rb") as f:
        return "ok" if sha256(f.read()) == entry["sha256"] else "changed"


def cmd_fetch():
    os.makedirs(DEST, exist_ok=True)
    lock = load_lock()
    for e in lock["files"]:
        state = check(e)
        if state == "ok":
            print(f"ok       {e['file']}")
            continue
        data = get(e["url"])
        if sha256(data) != e["sha256"]:
            sys.exit(f"error: {e['file']} does not match the lock file (sha256 {sha256(data)})")
        tmp = os.path.join(DEST, e["file"] + ".part")
        with open(tmp, "wb") as f:
            f.write(data)
        os.replace(tmp, os.path.join(DEST, e["file"]))
        print(f"fetched  {e['file']} ({len(data)} bytes, {e['license']})")
    with open(os.path.join(DEST, "CREDITS.txt"), "w", encoding="utf-8", newline="\n") as f:
        f.write(credits(lock))
    return cmd_verify()


def cmd_verify():
    bad = [(e["file"], s) for e in load_lock()["files"] if (s := check(e)) != "ok"]
    for f, s in bad:
        print(f"{s:8} {f}")
    print("testassets: " + ("all files match the lock" if not bad else f"{len(bad)} problem(s)"))
    return 1 if bad else 0


def credits(lock):
    lines = [f"Test assets from {lock['source']} (commit {lock['commit'][:12]}). Not part of the Bidulgi Engine repository."]
    for e in lock["files"]:
        if e["kind"] == "model":
            lines.append(f"- {e['name']}: {e['artist']} / {e['owner']} ({e['year']}), {e['license']}")
    return "\n".join(lines) + "\n"


def main(argv):
    cmd = argv[1] if len(argv) > 1 else "fetch"
    if cmd == "lock":
        cmd_lock()
        return 0
    if cmd == "fetch":
        return cmd_fetch()
    if cmd == "verify":
        return cmd_verify()
    if cmd == "credits":
        print(credits(load_lock()), end="")
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
