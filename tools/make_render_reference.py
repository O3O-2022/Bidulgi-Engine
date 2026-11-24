#!/usr/bin/env python3
"""Builds the rendering reference project (queue 3-8, Docs/features/graphics-test-scenes.md).

  python tools/make_render_reference.py        writes Tests/RenderReference/Assets/...

Output (all text or small generated pictures, committed): Scenes/RenderReference.scene, about a hundred
.material / .matinst files and two textures. Open it with
  BDG_ASSETS=Tests/RenderReference/Assets BDG_SCENE_FILE=Tests/RenderReference/Assets/Scenes/RenderReference.scene
Every zone has a camera bookmark: keys 1-9 over the scene view.

Zones along +X, 30 m apart (x = zone * 30):
  1 materials   7x7 white spheres (roughness ->, metallic ^) + gold and copper roughness rows
  2 lights      thin pillars, floating sphere, stairs, red point light, white spot light (sun shadows)
  3 alpha       coloured glass panes one behind another, a cut-out fence
  4 geometry    144 small cubes, a very thin bar, two overlapping planes (z-fighting)
  5 post        glowing spheres (emissive 12+), a dark tunnel
  6 reference   axis arrows, 18% grey card, ColorChecker, letter board (UV direction)
  7, 8          zone 1 again at the low and potato tiers (quality comparison)
  9             overview
Sponza is not used: the Khronos copy is under the CRYENGINE licence (see decisions.md).
"""
import json
import math
import os
import struct
import zlib

ROOT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "Tests", "RenderReference", "Assets")


def write(rel, data):
    path = os.path.join(ROOT, rel)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    mode = "wb" if isinstance(data, bytes) else "w"
    with open(path, mode, **({} if mode == "wb" else {"encoding": "utf-8", "newline": "\n"})) as f:
        f.write(data)


def png(w, h, pixel):  # pixel(x, y) -> (r, g, b, a); top row first
    raw = b"".join(b"\0" + b"".join(bytes(pixel(x, y)) for x in range(w)) for y in range(h))
    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) + \
        chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")


def hex_srgb(linear):  # linear 0..1 -> "#rrggbb"
    def enc(c):
        c = max(0.0, min(1.0, c))
        s = c * 12.92 if c <= 0.0031308 else 1.055 * c ** (1 / 2.4) - 0.055
        return round(s * 255)
    return "#" + "".join(f"{enc(c):02x}" for c in linear)


def material(name, shading="lit", blend="opaque", two_sided=False, cutoff=0.5, **params):
    lines = [{"material": name, "shading": shading, "blend": blend, "twoSided": two_sided, "cutoff": cutoff}]
    for key, value in params.items():
        kind = "texture" if key.endswith("Map") else "color" if isinstance(value, str) else "float"
        lines.append({"param": key, "type": kind, "value": value})
    write(f"Materials/{name}.material", "".join(json.dumps(l, separators=(",", ":")) + "\n" for l in lines))
    return name


def instance(name, parent, **sets):
    lines = [{"instance": name, "parent": parent}] + [{"set": k, "value": v} for k, v in sets.items()]
    write(f"Materials/{name}.matinst", "".join(json.dumps(l, separators=(",", ":")) + "\n" for l in lines))
    return name


class Scene:
    def __init__(self):
        self.lines = [{"v": 1, "defaults": "1.0", "scene": "RenderReference", "mode": "3d"}]
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

    def mesh(self, name, mesh, mat, parent=None, pos=None, rot=None, scale=None):
        return self.obj(name, parent, pos, rot, scale, MeshRenderer={"mesh": mesh, "material": mat})

    def bookmark(self, name, slot, pivot, yaw, pitch, distance, tier=""):
        b = {"slot": slot, "pivot": [round(x, 3) for x in pivot], "yaw": yaw, "pitch": pitch, "distance": distance}
        if tier:
            b["tier"] = tier
        return self.obj(name, self.bookmarks, CameraBookmark=b)

    def text(self):
        return "".join(json.dumps(l, separators=(",", ":"), ensure_ascii=False) + "\n" for l in self.lines)


# ColorChecker Classic, sRGB (same table as Engine/Render3D/ReferenceBoard.cpp)
CHECKER = ["#735244", "#c29682", "#627a9d", "#576c43", "#8580b1", "#67bdaa", "#d67e2c", "#505ba6", "#c15a63", "#5e3c6c",
           "#9dbc40", "#e0a32e", "#383d96", "#469449", "#af363c", "#e7c71f", "#bb5695", "#0885a1", "#f3f3f2", "#c8c8c8",
           "#a0a0a0", "#7a7a79", "#555555", "#343434"]


def main():
    # ---- textures ----
    def letters(x, y):  # "F" with coloured corners (UV direction), 64 x 64
        if x < 10 and y < 10: return (220, 40, 40, 255)
        if x >= 54 and y < 10: return (40, 200, 60, 255)
        if x < 10 and y >= 54: return (40, 80, 220, 255)
        if x >= 54 and y >= 54: return (240, 210, 40, 255)
        ink = (20 <= x < 28 and 14 <= y < 50) or (20 <= x < 46 and 14 <= y < 21) or (20 <= x < 40 and 29 <= y < 35)
        return (20, 20, 24, 255) if ink else (235, 235, 230, 255)
    write("Textures/T_Letters_D.png", png(64, 64, letters))

    def fence(x, y):  # diagonal wire mesh, holes transparent, 128 x 128
        d1, d2 = (x + y) % 32, (x - y) % 32
        wire = d1 < 3 or d2 < 3
        return (90, 92, 96, 255) if wire else (0, 0, 0, 0)
    write("Textures/T_Fence_D.png", png(128, 128, fence))

    # ---- materials ----
    material("M_Matrix", BaseColor=hex_srgb((0.85, 0.85, 0.85)), Metallic=0.0, Roughness=0.5)
    material("M_GoldMaster", BaseColor="#ffc356", Metallic=1.0, Roughness=0.3)
    material("M_CopperMaster", BaseColor="#fab499", Metallic=1.0, Roughness=0.3)
    for r in range(7):
        for m in range(7):
            instance(f"MI_Matrix_r{r}_m{m}", "M_Matrix", Roughness=round(r / 6, 4), Metallic=round(m / 6, 4))
        instance(f"MI_Gold_r{r}", "M_GoldMaster", Roughness=round(r / 6, 4))
        instance(f"MI_Copper_r{r}", "M_CopperMaster", Roughness=round(r / 6, 4))
    material("M_Ground", BaseColor=hex_srgb((0.22, 0.22, 0.23)), Roughness=0.9)
    material("M_Concrete", BaseColor=hex_srgb((0.5, 0.5, 0.48)), Roughness=0.85)
    material("M_Paint", BaseColor="#d8d8d0", Roughness=0.6)
    for name, c in (("Red", "#e04040"), ("Green", "#40c060"), ("Blue", "#4070e0"), ("Clear", "#e8f0ff")):
        material(f"M_Glass{name}", blend="translucent", two_sided=True, BaseColor=c, Roughness=0.05, Opacity=0.35)
    material("M_Fence", blend="masked", two_sided=True, cutoff=0.5, BaseColor="#ffffff", BaseColorMap="Textures/T_Fence_D.png",
             Metallic=1.0, Roughness=0.4)
    material("M_SmallCube", BaseColor="#8aa6c8", Roughness=0.5)
    material("M_ZFightA", shading="unlit", BaseColor="#d03030")
    material("M_ZFightB", shading="unlit", BaseColor="#3050d0")
    material("M_GlowWarm", BaseColor="#ffcc88", Emissive="#ffb060", EmissiveStrength=12.0)
    material("M_GlowCold", BaseColor="#88ccff", Emissive="#60b0ff", EmissiveStrength=20.0)
    material("M_Cave", BaseColor=hex_srgb((0.05, 0.045, 0.04)), Roughness=0.95)
    for axis, c in (("X", (0.9, 0.04, 0.04)), ("Y", (0.05, 0.75, 0.06)), ("Z", (0.04, 0.12, 0.95))):
        material(f"M_Axis{axis}", shading="unlit", BaseColor=hex_srgb(c))
    material("M_Grey18", shading="unlit", BaseColor=hex_srgb((0.18, 0.18, 0.18)))
    material("M_CheckerBack", shading="unlit", BaseColor=hex_srgb((0.02, 0.02, 0.02)))
    for i, c in enumerate(CHECKER):
        material(f"M_Checker{i:02d}", shading="unlit", BaseColor=c)
    material("M_Letters", shading="unlit", BaseColor="#ffffff", BaseColorMap="Textures/T_Letters_D.png")

    # ---- scene ----
    s = Scene()
    s.obj("Sun", rot=[-50, -30, 0], Light={"type": "Directional", "intensity": 3})
    s.obj("Sky", Sky={})
    s.mesh("Ground", "Plane", "M_Ground", pos=[75, 0, 0], scale=[220, 1, 60])
    s.bookmarks = s.obj("Bookmarks")

    # 1 materials: white matrix on a vertical wall, gold / copper rows in front
    z1 = s.obj("1 Materials", pos=[0, 0, 0])
    for r in range(7):
        for m in range(7):
            s.mesh(f"r{r} m{m}", "Sphere", f"MI_Matrix_r{r}_m{m}", z1, pos=[-2.4 + r * 0.8, 0.6 + m * 0.8, -1.0], scale=[0.64] * 3)
        s.mesh(f"gold r{r}", "Sphere", f"MI_Gold_r{r}", z1, pos=[-2.4 + r * 0.8, 0.35, 1.0], scale=[0.6] * 3)
        s.mesh(f"copper r{r}", "Sphere", f"MI_Copper_r{r}", z1, pos=[-2.4 + r * 0.8, 0.35, 2.0], scale=[0.6] * 3)
    s.bookmark("1 Materials", 1, [0, 2.4, 0], 0, -8, 10.5)

    # 2 lights and shadows
    z2 = s.obj("2 Lights", pos=[30, 0, 0])
    for i in range(4):
        s.mesh(f"pillar {i}", "Cube", "M_Concrete", z2, pos=[-3 + i * 1.2, 1.5, -1.5], scale=[0.08 + i * 0.06, 3, 0.08 + i * 0.06])
    s.mesh("floating sphere", "Sphere", "M_Paint", z2, pos=[1.8, 1.4, 0.5], scale=[0.8] * 3)
    for i in range(5):
        s.mesh(f"step {i}", "Cube", "M_Concrete", z2, pos=[3.2, 0.15 + i * 0.3, -1.5 + i * 0.4], scale=[1.2, 0.3, 0.4])
    s.obj("red point", z2, pos=[-1.6, 0.6, 1.2], Light={"type": "Point", "color": [1, 0.15, 0.1, 1], "intensity": 4, "range": 3})
    s.obj("white spot", z2, pos=[0.4, 3.2, -0.4], rot=[-90, 0, 0],
          Light={"type": "Spot", "intensity": 10, "range": 8, "spotAngle": 40, "innerSpotAngle": 24})
    s.bookmark("2 Lights", 2, [30, 1.2, 0], 20, -18, 9)

    # 3 transparency
    z3 = s.obj("3 Alpha", pos=[60, 0, 0])
    for i, name in enumerate(("Red", "Green", "Blue", "Clear")):
        s.mesh(f"glass {name}", "Cube", f"M_Glass{name}", z3, pos=[-1.5 + i * 0.6, 1.2, -1 + i * 0.7], scale=[1.4, 2, 0.04])
    s.mesh("fence", "Plane", "M_Fence", z3, pos=[2.4, 1.2, 0], rot=[90, -20, 0], scale=[2.4, 1, 2.4])
    s.mesh("behind the glass", "Sphere", "M_Paint", z3, pos=[-0.6, 0.6, -2.6], scale=[1.2] * 3)
    s.bookmark("3 Alpha", 3, [60, 1.2, 0], 15, -10, 7)

    # 4 geometry and culling
    z4 = s.obj("4 Geometry", pos=[90, 0, 0])
    for i in range(12):
        for j in range(12):
            s.mesh(f"cube {i},{j}", "Cube", "M_SmallCube", z4, pos=[-3 + i * 0.5, 0.1, -3 + j * 0.5], scale=[0.2] * 3)
    s.mesh("thin bar", "Cube", "M_Paint", z4, pos=[0, 1.6, 0], scale=[8, 0.01, 0.01])
    s.mesh("z-fight A", "Plane", "M_ZFightA", z4, pos=[3.6, 1.2, 0], rot=[90, 0, 0], scale=[1.2, 1, 1.2])
    s.mesh("z-fight B", "Plane", "M_ZFightB", z4, pos=[3.6, 1.2, 0], rot=[90, 0, 0], scale=[1.2, 1, 1.2])
    s.bookmark("4 Geometry", 4, [90, 0.5, 0], 25, -35, 10)

    # 5 post: glow and a dark tunnel
    z5 = s.obj("5 Post", pos=[120, 0, 0])
    s.mesh("glow warm", "Sphere", "M_GlowWarm", z5, pos=[-1.5, 1.0, 0], scale=[0.7] * 3)
    s.mesh("glow cold", "Sphere", "M_GlowCold", z5, pos=[-0.3, 0.6, 0.8], scale=[0.4] * 3)
    for name, pos, scale in (("tunnel left", [2.0, 1.2, -1], [0.4, 2.4, 4]), ("tunnel right", [4.0, 1.2, -1], [0.4, 2.4, 4]),
                             ("tunnel roof", [3.0, 2.6, -1], [2.4, 0.4, 4]), ("tunnel back", [3.0, 1.2, -3.1], [2.4, 2.4, 0.2])):
        s.mesh(name, "Cube", "M_Cave", z5, pos=pos, scale=scale)
    s.bookmark("5 Post", 5, [120, 1.2, 0], 0, -6, 8)

    # 6 coordinate and colour reference
    z6 = s.obj("6 Reference", pos=[150, 0, 0])
    for axis, rot in (("X", [0, 0, -90]), ("Y", [0, 0, 0]), ("Z", [90, 0, 0])):
        a = s.obj(f"axis {axis}", z6, pos=[0, 0.05, 0], rot=rot)
        s.mesh("shaft", "Cylinder", f"M_Axis{axis}", a, pos=[0, 0.45, 0], scale=[0.07, 0.9, 0.07])
        s.mesh("head", "Cone", f"M_Axis{axis}", a, pos=[0, 1.05, 0], scale=[0.2, 0.3, 0.2])
    s.mesh("grey card 18%", "Plane", "M_Grey18", z6, pos=[-3, 1.2, -1], rot=[90, 0, 0], scale=[1, 1, 1])
    s.mesh("letters", "Plane", "M_Letters", z6, pos=[-3, 2.6, -1], rot=[90, 0, 0], scale=[1.2, 1, 1.2])
    board = s.obj("ColorChecker", z6, pos=[3, 1.4, -1])
    s.mesh("backing", "Plane", "M_CheckerBack", board, pos=[0, 0, -0.01], rot=[90, 0, 0], scale=[2.44, 1, 1.68])
    for i in range(24):
        col, row = i % 6, i // 6
        s.mesh(f"patch {i:02d}", "Plane", f"M_Checker{i:02d}", board, pos=[(col - 2.5) * 0.38, (1.5 - row) * 0.38, 0],
               rot=[90, 0, 0], scale=[0.34, 1, 0.34])
    s.bookmark("6 Reference", 6, [150, 1.4, 0], 0, -10, 8)

    # 7, 8: zone 1 at lower tiers; 9: overview
    s.bookmark("7 Materials · low", 7, [0, 2.4, 0], 0, -8, 10.5, tier="low")
    s.bookmark("8 Materials · potato", 8, [0, 2.4, 0], 0, -8, 10.5, tier="potato")
    s.bookmark("9 Overview", 9, [75, 0, 0], 0, -35, 120)
    write("Scenes/RenderReference.scene", s.text())
    print(f"render reference: {len(s.lines) - 1} objects, written under {ROOT}")


if __name__ == "__main__":
    main()
