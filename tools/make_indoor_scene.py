#!/usr/bin/env python3
"""Builds the indoor composite test scene (queue 3-8b): a cloister hall with a side room.

  python tools/make_indoor_scene.py        writes Tests/IndoorScene/Assets/...

Everything is made here (procedural textures, materials, scene), so the licence is clear: CC0 1.0, see
Tests/IndoorScene/LICENSE.md. It stands in for Sponza (whose Khronos copy is under the CRYENGINE licence, see
decisions.md). Open it with
  BDG_ASSETS=Tests/IndoorScene/Assets BDG_SCENE_FILE=Tests/IndoorScene/Assets/Scenes/Cloister.scene
Camera bookmarks 1-9 (number keys over the scene view).

What it exercises:
  textures   brick, floor tiles, plaster, wood, a painting and an iron railing: colour (_D), normal (_N) and packed
             occlusion / roughness / metal (_ORM) maps, i.e. the world, normal and masks texture groups (3-5c)
  lights     a sun through five windows (shadows of the window piers and 12 columns), 6 lanterns (point lights,
             shadows), two spot lights on the painting, one point light with shadows in the side room
  GI         a reflection probe and a light probe volume for the hall, a reflection probe for the side room
  alpha      a masked railing (holes cut into the shadows too)
The hall runs along Z: x -5 .. 5, z -15 .. 15, 6 m high; windows in the east wall (+x); the side room is behind
a door in the north wall (z -15 .. -21).
"""
import json
import math
import os
import struct
import zlib

ROOT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "Tests", "IndoorScene", "Assets")


def write(rel, data):
    path = os.path.join(ROOT, rel)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    mode = "wb" if isinstance(data, bytes) else "w"
    with open(path, mode, **({} if mode == "wb" else {"encoding": "utf-8", "newline": "\n"})) as f:
        f.write(data)


def png(w, h, rows):  # rows: list of bytearrays (RGBA, top row first)
    raw = b"".join(b"\0" + bytes(r) for r in rows)
    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) + \
        chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")


def image(n, pixel):  # pixel(x, y) -> (r, g, b, a) in 0..255
    rows = []
    for y in range(n):
        row = bytearray()
        for x in range(n):
            row.extend(max(0, min(255, int(round(c)))) for c in pixel(x, y))
        rows.append(row)
    return png(n, n, rows)


def hash01(*v):  # deterministic value noise in 0..1 (no random module: same bytes on every machine)
    h = 2166136261
    for k in v:
        h = ((h ^ (k & 0xFFFFFFFF)) * 16777619) & 0xFFFFFFFF
    h ^= h >> 13
    h = (h * 0x5bd1e995) & 0xFFFFFFFF
    h ^= h >> 15
    return (h & 0xFFFFFF) / float(0xFFFFFF)


def smooth_noise(x, y, cell, seed):  # bilinear value noise, tiling over 512 px
    gx, gy = x / cell, y / cell
    x0, y0 = int(math.floor(gx)), int(math.floor(gy))
    fx, fy = gx - x0, gy - y0
    fx, fy = fx * fx * (3 - 2 * fx), fy * fy * (3 - 2 * fy)
    m = max(1, 512 // cell)
    def v(i, j):
        return hash01(i % m, j % m, seed)
    a = v(x0, y0) + (v(x0 + 1, y0) - v(x0, y0)) * fx
    b = v(x0, y0 + 1) + (v(x0 + 1, y0 + 1) - v(x0, y0 + 1)) * fx
    return a + (b - a) * fy


def normal_map(n, height, strength):  # tangent-space normal from a height function (tiling), OpenGL convention
    hs = [[height(x, y) for x in range(n)] for y in range(n)]
    def px(x, y):
        dx = (hs[y][(x + 1) % n] - hs[y][(x - 1) % n]) * strength
        dy = (hs[(y + 1) % n][x] - hs[(y - 1) % n][x]) * strength
        nx, ny, nz = -dx, dy, 1.0  # rows go down the picture while +V (tangent bitangent) goes up
        l = math.sqrt(nx * nx + ny * ny + nz * nz)
        return ((nx / l * 0.5 + 0.5) * 255, (ny / l * 0.5 + 0.5) * 255, (nz / l * 0.5 + 0.5) * 255, 255)
    return image(n, px)


# ---------------------------------------------------------------- textures
def brick_layout(x, y):  # 512 px = 1 m: 8 courses of 128 x 64 px bricks, 6 px mortar; returns (brick id, edge distance)
    row = y // 64
    off = 64 if row % 2 else 0
    col = ((x + off) % 512) // 128
    bx, by = (x + off) % 128, y % 64
    edge = min(bx, 127 - bx, by, 63 - by)
    return (row, col), edge


def textures():
    def brick_d(x, y):
        (r, c), edge = brick_layout(x, y)
        if edge < 3:
            g = 150 + 20 * smooth_noise(x, y, 8, 1)
            return (g, g * 0.97, g * 0.92, 255)
        t = hash01(r, c, 7)
        n = smooth_noise(x, y, 16, 2) * 0.25 + smooth_noise(x, y, 4, 3) * 0.1
        base = (150 + 50 * t, 70 + 25 * t, 50 + 15 * t)
        return tuple(b * (0.85 + n) for b in base) + (255,)
    write("Textures/T_Brick_D.png", image(512, brick_d))
    def brick_h(x, y):
        _, edge = brick_layout(x, y)
        return min(edge, 6) / 6.0 + smooth_noise(x, y, 8, 4) * 0.15
    write("Textures/T_Brick_N.png", normal_map(512, brick_h, 2.0))
    def brick_orm(x, y):
        _, edge = brick_layout(x, y)
        ao = 0.55 if edge < 3 else 0.8 + 0.2 * min(edge, 8) / 8
        return (ao * 255, (0.92 if edge < 3 else 0.8) * 255, 0, 255)
    write("Textures/T_Brick_ORM.png", image(512, brick_orm))

    def tile(x, y):  # 512 px = 1 m: 2 x 2 tiles of 256 px, 4 px grout
        tx, ty = x // 256, y // 256
        gx, gy = x % 256, y % 256
        return tx, ty, min(gx, 255 - gx, gy, 255 - gy)
    def tiles_d(x, y):
        tx, ty, edge = tile(x, y)
        if edge < 2:
            return (70, 68, 64, 255)
        light = (tx + ty) % 2 == 0
        vein = abs(math.sin((x * 0.03 + y * 0.011) + 6 * smooth_noise(x, y, 64, 5)))
        v = 1 - 0.25 * (vein ** 8)
        base = (228, 220, 204) if light else (92, 84, 80)
        return tuple(b * v * (0.93 + 0.07 * smooth_noise(x, y, 16, 6)) for b in base) + (255,)
    write("Textures/T_Tiles_D.png", image(512, tiles_d))
    write("Textures/T_Tiles_N.png", normal_map(512, lambda x, y: min(tile(x, y)[2], 4) / 4.0, 1.5))
    def tiles_orm(x, y):
        _, _, edge = tile(x, y)
        return ((0.6 if edge < 2 else 1.0) * 255, (0.9 if edge < 2 else 0.25 + 0.1 * smooth_noise(x, y, 32, 7)) * 255, 0, 255)
    write("Textures/T_Tiles_ORM.png", image(512, tiles_orm))

    def plaster_d(x, y):  # 256 px = 1 m
        n = smooth_noise(x * 2, y * 2, 32, 8) * 0.08 + smooth_noise(x * 2, y * 2, 8, 9) * 0.04
        return (222 * (0.92 + n), 214 * (0.92 + n), 196 * (0.92 + n), 255)
    write("Textures/T_Plaster_D.png", image(256, plaster_d))
    write("Textures/T_Plaster_N.png", normal_map(256, lambda x, y: smooth_noise(x * 2, y * 2, 16, 10), 1.2))

    def wood_d(x, y):  # 256 px = 1 m: planks along x, 32 px wide
        plank = y // 32
        t = hash01(plank, 11)
        grain = 0.5 + 0.5 * math.sin((y % 32) * 0.6 + 8 * smooth_noise(x * 2 + plank * 97, y * 2, 64, 12) + t * 6)
        seam = 0.55 if y % 32 < 1 else 1.0
        base = (128 + 40 * t, 82 + 25 * t, 48 + 12 * t)
        return tuple(b * (0.8 + 0.2 * grain) * seam for b in base) + (255,)
    write("Textures/T_Wood_D.png", image(256, wood_d))
    write("Textures/T_Wood_N.png", normal_map(256, lambda x, y: (0.0 if y % 32 < 1 else 1.0) + 0.1 * math.sin((y % 32) * 0.6), 1.0))

    def painting_d(x, y):  # 512 x 512 picture: dusk sky, sun, hills; red top-left marker (UV direction)
        if x < 24 and y < 24:
            return (220, 40, 40, 255)
        u, v = x / 511, y / 511
        sky = (40 + 180 * v, 50 + 90 * v, 120 - 40 * v)
        d = math.hypot(u - 0.65, v - 0.35)
        if d < 0.1:
            sky = (250, 210, 120)
        hill = 0.62 + 0.08 * math.sin(u * 9) + 0.04 * math.sin(u * 23 + 1)
        if v > hill:
            sky = (40 + 30 * smooth_noise(x, y, 16, 13), 70 + 40 * smooth_noise(x, y, 32, 14), 40)
        frame = min(x, 511 - x, y, 511 - y) < 18
        return (90, 60, 30, 255) if frame else sky + (255,)
    write("Textures/T_Painting_D.png", image(512, painting_d))

    def railing_d(x, y):  # 256 px = 1 m: bars every 32 px, top and bottom rails; holes transparent
        bar = (x % 32) < 5
        rail = y < 14 or y > 241 or abs(y - 128) < 4
        ring = abs(math.hypot((x % 64) - 32, (y % 128) - 64) - 22) < 2.5 and 14 < y < 242
        return (40, 40, 44, 255) if (bar or rail or ring) else (0, 0, 0, 0)
    write("Textures/T_Railing_D.png", image(256, railing_d))


# ---------------------------------------------------------------- materials and scene
def material(name, shading="lit", blend="opaque", two_sided=False, cutoff=0.5, **params):
    lines = [{"material": name, "shading": shading, "blend": blend, "twoSided": two_sided, "cutoff": cutoff}]
    for key, value in params.items():
        kind = "texture" if key.endswith("Map") else "color" if isinstance(value, str) else "float"
        lines.append({"param": key, "type": kind, "value": value})
    write(f"Materials/{name}.material", "".join(json.dumps(l, separators=(",", ":")) + "\n" for l in lines))
    return name


class Scene:
    def __init__(self, name):
        self.lines = [{"v": 1, "defaults": "1.0", "scene": name, "mode": "3d"}]
        self.next = 1

    def obj(self, name, parent=None, pos=None, rot=None, scale=None, **components):
        oid = f"g{self.next}"
        self.next += 1
        line = {"id": oid, "name": name}
        if parent:
            line["parent"] = "@" + parent
        for key, v in (("pos", pos), ("rot", rot), ("scale", scale)):
            if v is not None:
                line[key] = [round(x, 4) for x in v]
        line.update(components)
        self.lines.append(line)
        return oid

    def block(self, name, mat, pos, size, parent=None, shape="Box"):  # pivot: bottom centre, world-scale UVs
        b = {"size": [round(x, 4) for x in size], "material": mat}
        if shape != "Box":
            b["shape"] = shape
        return self.obj(name, parent, pos=pos, Blockout=b)

    def mesh(self, name, mesh, mat, parent=None, pos=None, rot=None, scale=None):
        return self.obj(name, parent, pos, rot, scale, MeshRenderer={"mesh": mesh, "material": mat})

    def bookmark(self, name, slot, pivot, yaw, pitch, distance, tier=""):
        b = {"slot": slot, "pivot": [round(x, 3) for x in pivot], "yaw": yaw, "pitch": pitch, "distance": distance}
        if tier:
            b["tier"] = tier
        return self.obj(name, self.bookmarks, CameraBookmark=b)

    def text(self):
        return "".join(json.dumps(l, separators=(",", ":"), ensure_ascii=False) + "\n" for l in self.lines)


WINDOWS = [-10, -5, 0, 5, 10]   # window centres along the east wall (2 m wide, 1.5 .. 4.5 m high)
COLUMNS = [-12.5, -7.5, -2.5, 2.5, 7.5, 12.5]
LANTERNS = [-12.5, -7.5, -2.5, 2.5, 7.5, 12.5]


def scene():
    material("M_Brick", BaseColor="#ffffff", BaseColorMap="Textures/T_Brick_D.png", NormalMap="Textures/T_Brick_N.png",
             ORMMap="Textures/T_Brick_ORM.png", Roughness=1.0, Metallic=0.0)
    material("M_Tiles", BaseColor="#ffffff", BaseColorMap="Textures/T_Tiles_D.png", NormalMap="Textures/T_Tiles_N.png",
             ORMMap="Textures/T_Tiles_ORM.png", Roughness=1.0, Metallic=0.0)
    material("M_Plaster", BaseColor="#ffffff", BaseColorMap="Textures/T_Plaster_D.png", NormalMap="Textures/T_Plaster_N.png",
             Roughness=0.85)
    material("M_Stone", BaseColor="#b8b2a6", BaseColorMap="Textures/T_Plaster_D.png", NormalMap="Textures/T_Plaster_N.png",
             Roughness=0.7)
    material("M_Wood", BaseColor="#ffffff", BaseColorMap="Textures/T_Wood_D.png", NormalMap="Textures/T_Wood_N.png", Roughness=0.6)
    material("M_Painting", BaseColor="#ffffff", BaseColorMap="Textures/T_Painting_D.png", Roughness=0.45)
    material("M_Railing", blend="masked", two_sided=True, cutoff=0.5, BaseColor="#ffffff", BaseColorMap="Textures/T_Railing_D.png",
             Metallic=1.0, Roughness=0.45)
    material("M_Bulb", BaseColor="#fff0d0", Emissive="#ffc880", EmissiveStrength=8.0)
    material("M_Gold", BaseColor="#ffc356", Metallic=1.0, Roughness=0.25)
    material("M_Copper", BaseColor="#fab499", Metallic=1.0, Roughness=0.4)

    s = Scene("Cloister")
    s.obj("Sun", rot=[-32, 78, 0], Light={"type": "Directional", "intensity": 6})  # low, from the east windows (+x)
    s.obj("Sky", Sky={"intensity": 0.4})  # dim: the GI probes cannot tell a closed room from the open sky
    s.bookmarks = s.obj("Bookmarks")

    hall = s.obj("Hall")
    s.block("floor", "M_Tiles", [0, -0.2, 0], [10, 0.2, 30], hall)
    s.block("ceiling", "M_Plaster", [0, 6, 0], [10.8, 0.3, 30.8], hall)
    s.block("west wall", "M_Brick", [-5.2, 0, 0], [0.4, 6, 30.8], hall)
    s.block("south wall", "M_Brick", [0, 0, 15.2], [10.8, 6, 0.4], hall)
    s.block("east wall low", "M_Brick", [5.2, 0, 0], [0.4, 1.5, 30.8], hall)
    s.block("east wall high", "M_Brick", [5.2, 4.5, 0], [0.4, 1.5, 30.8], hall)
    edges = [-15.4] + [e for c in WINDOWS for e in (c - 1, c + 1)] + [15.4]
    for i in range(0, len(edges), 2):  # piers between the windows
        z0, z1 = edges[i], edges[i + 1]
        s.block(f"east pier {i // 2}", "M_Brick", [5.2, 1.5, (z0 + z1) / 2], [0.4, 3, z1 - z0], hall)
    s.block("north wall left", "M_Brick", [-3.2, 0, -15.2], [4.4, 6, 0.4], hall)
    s.block("north wall right", "M_Brick", [3.2, 0, -15.2], [4.4, 6, 0.4], hall)
    s.block("door lintel", "M_Brick", [0, 3, -15.2], [2, 3, 0.4], hall)
    for side in (-3, 3):
        for z in COLUMNS:
            s.block(f"column {'w' if side < 0 else 'e'} {z:+.1f}", "M_Stone", [side, 0, z], [0.6, 6, 0.6], hall, shape="Cylinder")
    for z in (-6, 6):
        s.block(f"bench {z:+d}", "M_Wood", [-1.6, 0, z], [0.5, 0.45, 2.4], hall)
    s.mesh("painting", "Plane", "M_Painting", hall, pos=[-4.97, 2.7, 0], rot=[90, 90, 0], scale=[3, 1, 2])  # facing +x, upright
    s.mesh("railing", "Plane", "M_Railing", hall, pos=[-3.9, 0.5, 0], rot=[90, 90, 0], scale=[6, 1, 1])
    s.obj("hall probe", hall, pos=[0, 3, 0], ReflectionProbe={"size": [10, 6, 30]})
    s.obj("hall volume", hall, pos=[0, 3, 0], LightProbeVolume={"size": [10, 6, 30], "countX": 3, "countY": 2, "countZ": 8})

    lights = s.obj("Lights")
    for i, z in enumerate(LANTERNS):
        s.obj(f"lantern {i + 1}", lights, pos=[0, 4.2, z], Light={"type": "Point", "color": [1, 0.72, 0.45, 1], "intensity": 5, "range": 9})
        s.mesh(f"bulb {i + 1}", "Sphere", "M_Bulb", lights, pos=[0, 4.45, z], scale=[0.22, 0.22, 0.22])
    for i, z in enumerate((-1.2, 1.2)):  # aimed at the painting on the west wall
        s.obj(f"painting spot {i + 1}", lights, pos=[-1.6, 5.6, z], rot=[-44, 90, 0],
              Light={"type": "Spot", "intensity": 18, "range": 10, "spotAngle": 36, "innerSpotAngle": 22})

    room = s.obj("Side room")
    s.block("room floor", "M_Wood", [0, -0.2, -18.4], [6, 0.2, 6], room)
    s.block("room ceiling", "M_Plaster", [0, 4, -18.4], [6.8, 0.3, 6.8], room)
    s.block("room west", "M_Plaster", [-3.2, 0, -18.4], [0.4, 4, 6.8], room)
    s.block("room east", "M_Plaster", [3.2, 0, -18.4], [0.4, 4, 6.8], room)
    s.block("room back", "M_Plaster", [0, 0, -21.6], [6.8, 4, 0.4], room)
    for x, mat in ((-1.4, "M_Gold"), (1.4, "M_Copper")):
        s.block(f"pedestal {mat[2:]}", "M_Stone", [x, 0, -19.6], [0.6, 1.0, 0.6], room)
        s.mesh(f"sphere {mat[2:]}", "Sphere", mat, room, pos=[x, 1.3, -19.6], scale=[0.6, 0.6, 0.6])
    s.block("crate", "M_Wood", [2.0, 0, -16.6], [0.8, 0.8, 0.8], room)
    s.obj("room lamp", room, pos=[0, 2.2, -17.2],  # in front of the pedestals: their shadows reach the back wall
           Light={"type": "Point", "color": [1, 0.85, 0.7, 1], "intensity": 30, "range": 8})
    s.obj("room probe", room, pos=[0, 2, -18.4], ReflectionProbe={"size": [6, 4, 6]})

    s.bookmark("1 Hall", 1, [0, 2.2, -2], 0, -4, 15)
    s.bookmark("2 Window light", 2, [1.5, 0.4, -2.5], -62, -30, 5.5)
    s.bookmark("3 Painting (spots)", 3, [-4.6, 2.4, 0], 90, -6, 6)
    s.bookmark("4 Side room (point shadows)", 4, [0, 1.0, -19.2], 0, -14, 3.6)
    s.bookmark("5 Railing (alpha)", 5, [-3.9, 0.6, 1.5], 120, -12, 3.2)
    s.bookmark("6 Columns from above", 6, [0, 0, 0], 0, -55, 12)
    s.bookmark("7 Hall · low", 7, [0, 2.2, -2], 0, -4, 15, tier="low")
    s.bookmark("8 Hall · potato", 8, [0, 2.2, -2], 0, -4, 15, tier="potato")
    s.bookmark("9 Hall · ultra", 9, [0, 2.2, -2], 0, -4, 15, tier="ultra")
    write("Scenes/Cloister.scene", s.text())
    return len(s.lines) - 1


def main():
    textures()
    n = scene()
    print(f"indoor scene: {n} objects, written under {ROOT}")


if __name__ == "__main__":
    main()
