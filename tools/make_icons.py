#!/usr/bin/env python3
"""Editor icon set (64 x 64 PNG) drawn with PhotoCraft's headless CLI: the source of truth for Assets/Icons.

    python tools/make_icons.py                 # every icon
    python tools/make_icons.py tool-move file  # just these
    python tools/make_icons.py --sheet out.png # also a contact sheet (all icons at 64, 32 and 16 px)

Each icon is a list of PhotoCraft engine commands (vector shape layers, gradients, text) on a transparent 64 x 64
canvas: content sits inside 4..60, line icons use 5 px round strokes, colours come from the editor's design tokens
(design-system/project/tokens.json). The editor packs the PNGs into one atlas at start-up (UIContext::Icon).
PhotoCraft: set PHOTOCRAFT_CLI, or it is looked for in "C:/Program Files/PhotoCraft/photocraft-cli.exe".
"""
import json
import math
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "Assets" / "Icons"
CLI = os.environ.get("PHOTOCRAFT_CLI", r"C:\Program Files\PhotoCraft\photocraft-cli.exe")
FONT = "Arial-BoldMT"

# design tokens (dark editor theme)
FG, DIM, FAINT = "#e4e8ee", "#aab3c0", "#6c7583"
ACCENT, WARN, ERR, OK = "#f59e32", "#e6c057", "#ef6b62", "#72c04e"
X, Y, Z = "#e0564e", "#72c04e", "#3996b9"
PREFAB, CPP, CS, PY, GRAPH = "#3996b9", "#4b8ee6", "#a172e0", "#e3c03a", "#2bb3a3"
PAPER, PAPER_EDGE = "#dfe5ec", "#b9c3cf"
STROKE = 5


class Icon:
    def __init__(self):
        self.ops = []

    def shape(self, **p):
        self.ops.append(("shape.create", p))
        return self

    # --- primitives -------------------------------------------------------------------------------------------
    def rect(self, x, y, w, h, fill, r=0, stroke=None):
        return self.shape(kind="roundedRect" if r else "rect", rect=[x, y, w, h], radii=r, fill=fill, stroke=stroke)

    def ellipse(self, cx, cy, rx, ry, fill, stroke=None):
        return self.shape(kind="ellipse", rect=[cx - rx, cy - ry, 2 * rx, 2 * ry], fill=fill, stroke=stroke)

    def poly(self, pts, fill, stroke=None):
        return self.shape(kind="path", path={"subpaths": [{"closed": True, "knots": [list(p) for p in pts]}]}, fill=fill, stroke=stroke)

    def lines(self, pts, color, width=STROKE, closed=False, cap="round"):
        return self.shape(kind="path", path={"subpaths": [{"closed": closed, "knots": [list(p) for p in pts]}]}, fill=None,
                          stroke={"width": width, "color": color, "cap": cap, "join": "round", "align": "center"})

    def arc(self, cx, cy, r, a0, a1, color, width=STROKE, steps=28):
        pts = [(cx + r * math.cos(math.radians(a0 + (a1 - a0) * i / steps)), cy - r * math.sin(math.radians(a0 + (a1 - a0) * i / steps)))
               for i in range(steps + 1)]
        return self.lines(pts, color, width)

    def head(self, tip, toward, color, size=11, width=None):
        """Arrowhead at `tip` pointing away from `toward` (a filled triangle)."""
        dx, dy = tip[0] - toward[0], tip[1] - toward[1]
        d = math.hypot(dx, dy) or 1
        ux, uy = dx / d, dy / d
        w = width if width is not None else size * 0.8
        base = (tip[0] - ux * size, tip[1] - uy * size)
        return self.poly([tip, (base[0] - uy * w, base[1] + ux * w), (base[0] + uy * w, base[1] - ux * w)], color)

    def arrow(self, a, b, color, width=STROKE, size=11):
        dx, dy = b[0] - a[0], b[1] - a[1]
        d = math.hypot(dx, dy)
        end = (b[0] - dx / d * size * 0.6, b[1] - dy / d * size * 0.6)
        self.lines([a, end], color, width)
        return self.head(b, a, color, size)

    def text(self, x, y, s, size, color, font=FONT):
        self.ops.append(("type.create", {"x": x, "y": y, "text": s, "align": "center", "font": font, "size": size, "color": color}))
        return self

    # --- shared pieces ----------------------------------------------------------------------------------------
    def cube(self, cx, cy, s, top, left, right, outline=None):
        """Isometric cube centred at (cx, cy), edge `s`."""
        h = s * 0.5
        t = [(cx, cy - s), (cx + s * 0.87, cy - h), (cx, cy), (cx - s * 0.87, cy - h)]
        l = [(cx - s * 0.87, cy - h), (cx, cy), (cx, cy + s), (cx - s * 0.87, cy + h)]
        r = [(cx, cy), (cx + s * 0.87, cy - h), (cx + s * 0.87, cy + h), (cx, cy + s)]
        self.poly(t, top).poly(l, left).poly(r, right)
        if outline:
            self.lines([t[0], t[1], r[2], r[3], l[3], l[0]], outline, 3, closed=True)
            self.lines([t[3], t[2], t[1]], outline, 3).lines([t[2], r[3]], outline, 3)
        return self

    def doc(self, band=None, label=None, label_size=13):
        """Paper sheet with a folded corner; optional coloured band with a label at the bottom."""
        self.poly([(12, 4), (40, 4), (52, 16), (52, 60), (12, 60)],
                  {"gradient": {"stops": [[0, "#eef2f6"], [1, PAPER]], "angle": -90, "style": "linear"}},
                  {"width": 2, "color": PAPER_EDGE, "align": "inside", "join": "round"})
        self.poly([(40, 4), (40, 16), (52, 16)], PAPER_EDGE)
        if band:
            self.rect(6, 36, 40, 20, band, r=4)
            if label:
                self.text(26, 51, label, label_size, "#ffffff")
        return self

    def sphere(self, cx, cy, r, light, mid, dark):
        self.ellipse(cx, cy, r, r, {"gradient": {"stops": [[0, light], [0.55, mid], [1, dark]], "style": "radial", "scale": 140, "angle": 135}})
        return self.ellipse(cx - r * 0.35, cy - r * 0.38, r * 0.22, r * 0.16, [1, 1, 1, 0.65])

    def mountains(self, x, y, w, h, color, sun=None):
        self.poly([(x, y + h), (x + w * 0.38, y + h * 0.25), (x + w * 0.62, y + h * 0.62), (x + w * 0.75, y + h * 0.45), (x + w, y + h)], color)
        if sun:
            self.ellipse(x + w * 0.78, y + h * 0.05, w * 0.1, w * 0.1, sun)
        return self


def V(a, b):
    return {"gradient": {"stops": [[0, a], [1, b]], "angle": -90, "style": "linear"}}


ICONS = {}


def icon(name):
    def deco(fn):
        ICONS[name] = fn
        return fn
    return deco


# ---------------------------------------------------------------- tools (light line icons, accent detail) -------
@icon("tool-select")
def _(i):
    i.poly([(18, 8), (18, 50), (28, 40), (35, 56), (43, 52), (36, 37), (50, 37)], FG, {"width": 2, "color": "#0f1318", "join": "round", "align": "outside"})


@icon("tool-move")
def _(i):
    for a, b in [((32, 32), (32, 6)), ((32, 32), (32, 58)), ((32, 32), (6, 32)), ((32, 32), (58, 32))]:
        i.arrow(a, b, FG, size=12)
    i.ellipse(32, 32, 5, 5, ACCENT)


@icon("tool-rotate")
def _(i):
    i.arc(32, 32, 20, 110, 400, FG)
    i.head((32 + 20 * math.cos(math.radians(40)) + 1, 32 - 20 * math.sin(math.radians(40)) + 9), (52, 12), FG, size=13)
    i.ellipse(32, 32, 5, 5, ACCENT)


@icon("tool-scale")
def _(i):
    i.rect(8, 8, 48, 48, None, r=4, stroke={"width": 4, "color": DIM, "dashes": [2, 1.5], "cap": "round"})
    i.rect(8, 34, 22, 22, ACCENT, r=3)
    i.arrow((24, 40), (54, 10), FG, size=12)


@icon("tool-view")
def _(i):
    i.shape(kind="path", path={"subpaths": [{"closed": True, "knots": [
        {"anchor": [4, 32], "in": [4, 32], "out": [16, 14]}, {"anchor": [60, 32], "in": [48, 14], "out": [48, 50]},
        {"anchor": [4, 32], "in": [16, 50], "out": [4, 32]}]}]}, fill=None,
        stroke={"width": STROKE, "color": FG, "join": "round", "align": "center"})
    i.ellipse(32, 32, 10, 10, ACCENT).ellipse(32, 32, 4, 4, "#1f1305")


@icon("tool-measure")
def _(i):
    i.poly([(4, 42), (42, 4), (60, 22), (22, 60)], V("#f2d27a", WARN), {"width": 2, "color": "#a8862f", "align": "inside", "join": "round"})
    for k in range(1, 8):
        x, y = 4 + k * 4.75, 42 - k * 4.75
        ln = 9 if k % 2 == 0 else 5
        i.lines([(x, y), (x + ln * 0.707, y + ln * 0.707)], "#5a4513", 2.5)


@icon("tool-blockout")
def _(i):
    i.cube(30, 38, 20, "#c9d0d9", "#9aa3ae", "#b3bbc5")
    i.arrow((30, 24), (30, 3), ACCENT, width=5, size=11)


@icon("tool-snap")
def _(i):
    i.lines([(16, 8), (16, 34)], X, 12, cap="butt")
    i.lines([(48, 8), (48, 34)], X, 12, cap="butt")
    i.arc(32, 34, 16, 180, 360, X, width=12, steps=24)
    i.rect(10, 4, 12, 9, DIM).rect(42, 4, 12, 9, DIM)


# ---------------------------------------------------------------- components (coloured glyphs) -----------------
@icon("comp-transform")
def _(i):
    o = (22, 42)
    i.arrow(o, (58, 42), X, size=12)
    i.arrow(o, (22, 6), Y, size=12)
    i.arrow(o, (6, 58), Z, size=12)
    i.ellipse(22, 42, 5, 5, FG)


@icon("comp-mesh")
def _(i):
    i.cube(32, 32, 24, "#c3cad3", "#7d8693", "#a0a9b5", outline="#eef2f6")  # geometry: grey (prefabs are blue)


@icon("comp-light")
def _(i):
    i.ellipse(32, 26, 20, 20, {"gradient": {"stops": [[0, "#fff4c8"], [0.6, WARN], [1, "#d89a2b"]], "style": "radial", "scale": 130}})
    i.poly([(22, 40), (42, 40), (38, 50), (26, 50)], "#d89a2b")
    i.rect(24, 50, 16, 5, DIM, r=2).rect(26, 56, 12, 4, FAINT, r=2)


@icon("comp-sun")
def _(i):
    for k in range(8):
        a = math.radians(k * 45)
        i.lines([(32 + 19 * math.cos(a), 32 + 19 * math.sin(a)), (32 + 27 * math.cos(a), 32 + 27 * math.sin(a))], WARN, 5)
    i.ellipse(32, 32, 13, 13, {"gradient": {"stops": [[0, "#fff1b8"], [1, ACCENT]], "style": "radial", "scale": 120}})


@icon("comp-camera")
def _(i):
    i.rect(6, 18, 36, 30, V("#c3cbd5", "#8c96a3"), r=6)
    i.poly([(44, 26), (58, 18), (58, 48), (44, 40)], "#8c96a3")
    i.ellipse(24, 33, 9, 9, "#1c222b").ellipse(24, 33, 5, 5, Z)
    i.ellipse(14, 14, 6, 6, "#8c96a3").ellipse(30, 14, 6, 6, "#8c96a3")


@icon("comp-bookmark")
def _(i):
    i.poly([(16, 6), (48, 6), (48, 58), (32, 46), (16, 58)], V("#ffc06d", ACCENT))
    i.ellipse(32, 22, 7, 7, "#1f1305")


@icon("comp-reflection-probe")
def _(i):
    i.sphere(32, 32, 26, "#ffffff", "#9fb2c4", "#3c4a5a")
    i.ellipse(40, 44, 9, 5, [1, 1, 1, 0.18])  # the room reflected low on the ball


@icon("comp-light-probe")
def _(i):
    pts = [(12 + 20 * x, 12 + 20 * y) for y in range(3) for x in range(3)]
    for y in range(3):
        i.lines([pts[y * 3], pts[y * 3 + 2]], FAINT, 2)
    for x in range(3):
        i.lines([pts[x], pts[6 + x]], FAINT, 2)
    for p in pts:
        i.sphere(p[0], p[1], 7, "#fff6d0", WARN, "#a5781f")


@icon("comp-sky")
def _(i):
    i.rect(4, 6, 56, 52, V("#2f6fb0", "#9fd0f0"), r=10)
    i.ellipse(46, 18, 6, 6, "#fff1b8")
    for cx, cy, r in [(20, 40, 9), (30, 34, 11), (41, 40, 9)]:
        i.ellipse(cx, cy, r, r, "#ffffff")
    i.rect(12, 40, 38, 9, "#ffffff", r=4)


@icon("comp-color-grading")
def _(i):
    i.ellipse(32, 22, 16, 16, [0.88, 0.34, 0.31, 0.85])
    i.ellipse(22, 40, 16, 16, [0.45, 0.75, 0.31, 0.85])
    i.ellipse(42, 40, 16, 16, [0.22, 0.59, 0.73, 0.85])


@icon("comp-blockout")
def _(i):
    i.poly([(6, 56), (6, 44), (19, 44), (19, 32), (32, 32), (32, 20), (45, 20), (45, 8), (58, 8), (58, 56)], V("#d3d9e0", "#9aa3ae"))
    i.lines([(6, 44), (19, 44), (19, 32), (32, 32), (32, 20), (45, 20), (45, 8), (58, 8)], "#f2f5f8", 3)
    i.lines([(32, 24), (32, 56)], "#8a939e", 1.5).lines([(19, 36), (19, 56)], "#8a939e", 1.5).lines([(45, 12), (45, 56)], "#8a939e", 1.5)


@icon("comp-script")
def _(i):
    i.rect(4, 8, 56, 48, V("#2d2440", "#1c1830"), r=8, stroke={"width": 3, "color": CS, "align": "inside"})
    i.lines([(24, 20), (14, 32), (24, 44)], CS, 5)
    i.lines([(40, 20), (50, 32), (40, 44)], CS, 5)
    i.lines([(35, 18), (29, 46)], FG, 4)


@icon("comp-component")
def _(i):
    i.rect(8, 8, 48, 48, "#262e39", r=8, stroke={"width": 4, "color": DIM, "align": "inside"})
    i.rect(22, 22, 20, 20, DIM, r=3)


# ---------------------------------------------------------------- hierarchy ----------------------------------
@icon("obj-scene")
def _(i):
    i.rect(6, 10, 52, 44, "#2a2116", r=6, stroke={"width": 4, "color": ACCENT, "align": "inside"})
    i.mountains(12, 22, 40, 26, ACCENT, sun=WARN)


@icon("obj-prefab")
def _(i):
    i.cube(32, 32, 24, "#7cc0e0", "#2a6f8c", PREFAB)


@icon("obj-object")
def _(i):
    i.cube(32, 32, 24, [0, 0, 0, 0], [0, 0, 0, 0], [0, 0, 0, 0], outline=DIM)


# ---------------------------------------------------------------- folders and files --------------------------
@icon("folder")
def _(i):
    i.rect(4, 10, 26, 14, "#c9953b", r=4)
    i.rect(4, 16, 56, 40, V("#f6c760", "#e2a23c"), r=5)


@icon("folder-open")
def _(i):
    i.rect(4, 10, 26, 14, "#c9953b", r=4)
    i.rect(4, 16, 54, 40, "#c9953b", r=5)
    i.rect(10, 20, 40, 20, PAPER, r=2)
    i.poly([(4, 56), (12, 28), (62, 28), (54, 56)], V("#f8cf70", "#e2a23c"))


@icon("file")
def _(i):
    i.doc()
    for y in (24, 32, 40, 48):
        i.lines([(20, y), (44 if y < 48 else 34, y)], "#a7b2bf", 3)


@icon("file-scene")
def _(i):
    i.doc().rect(6, 30, 40, 28, "#2a2116", r=5, stroke={"width": 3, "color": ACCENT, "align": "inside"})
    i.mountains(11, 36, 30, 18, ACCENT, sun=WARN)


@icon("file-material")
def _(i):
    i.sphere(32, 32, 27, "#ffe2b0", ACCENT, "#7a3d10")


@icon("file-matinst")
def _(i):
    i.sphere(24, 26, 20, "#bfe6f5", "#3996b9", "#16384a")
    i.sphere(40, 40, 20, "#ffe2b0", ACCENT, "#7a3d10")


@icon("file-texture")
def _(i):
    i.rect(4, 8, 56, 48, V("#4a86c0", "#a9d4ef"), r=6, stroke={"width": 3, "color": "#e4e8ee", "align": "inside"})
    i.mountains(7, 22, 50, 31, "#4f8f45", sun="#fff1b8")


@icon("file-model")
def _(i):
    i.cube(32, 32, 26, "#a8b8ff", "#4b5fc2", "#7186e0", outline="#e6ebff")


@icon("file-cs")
def _(i):
    i.doc(CS, "C#", 15)


@icon("file-h")
def _(i):
    i.doc("#3b6fc0", ".H", 15)


@icon("file-cpp")
def _(i):
    i.doc(CPP, "C++", 13)


@icon("file-prefab")
def _(i):
    i.doc().cube(28, 44, 13, "#7cc0e0", "#2a6f8c", PREFAB)


@icon("file-audio")
def _(i):
    i.doc().rect(6, 30, 40, 28, "#1d3a2a", r=5)
    i.lines([(30, 50), (30, 33), (40, 36)], OK, 4)
    i.ellipse(25, 50, 6, 5, OK)


@icon("file-lut")
def _(i):
    i.doc()
    i.ellipse(24, 38, 9, 9, [0.88, 0.34, 0.31, 0.9]).ellipse(18, 48, 9, 9, [0.45, 0.75, 0.31, 0.9]).ellipse(30, 48, 9, 9, [0.22, 0.59, 0.73, 0.9])


@icon("file-json")
def _(i):
    i.doc("#4a3f12", "{ }", 14)


@icon("file-shader")
def _(i):
    i.doc().rect(6, 30, 40, 28, "#123a3a", r=5)
    i.shape(kind="star", rect=[14, 32, 24, 24], sides=4, starRatio=0.35, fill=GRAPH)


# ---------------------------------------------------------------- build --------------------------------------
def render(name, out_dir):
    i = Icon()
    ICONS[name](i)
    args = [CLI, "run", "--new", json.dumps({"width": 64, "height": 64, "background": "transparent", "name": name})]
    for cmd, params in i.ops:
        params = {k: v for k, v in params.items() if not (k == "stroke" and v is None) and not (k == "radii" and not v)}
        args += ["--cmd", cmd, "--params", json.dumps(params)]
    out = Path(out_dir) / f"{name}.png"
    args += ["--out", str(out)]
    r = subprocess.run(args, capture_output=True, text=True, encoding="utf-8", errors="replace")
    problems = [l for l in (r.stdout + r.stderr).splitlines() if ("error" in l.lower() or "not installed" in l) and not l.startswith('{"command"')]
    if r.returncode != 0 or not out.exists() or problems:
        raise SystemExit(f"{name}: PhotoCraft failed ({r.returncode})\n" + "\n".join(problems or (r.stdout + r.stderr).splitlines()[-5:]))
    return out


def contact_sheet(files, path):
    """All icons on the panel colour at 64, 32 and 16 px, with names (a preview; needs Pillow)."""
    from PIL import Image, ImageDraw
    cols, cw, ch = 8, 132, 104
    rows = (len(files) + cols - 1) // cols
    sheet = Image.new("RGBA", (cols * cw, rows * ch), (28, 34, 43, 255))
    d = ImageDraw.Draw(sheet)
    for k, f in enumerate(files):
        x, y = (k % cols) * cw + 8, (k // cols) * ch + 6
        im = Image.open(f).convert("RGBA")
        sheet.alpha_composite(im, (x, y))
        sheet.alpha_composite(im.resize((32, 32), Image.LANCZOS), (x + 72, y))
        sheet.alpha_composite(im.resize((16, 16), Image.LANCZOS), (x + 72, y + 40))
        d.text((x, y + 76), f.stem, fill=(170, 179, 192, 255))
    sheet.save(path)


def main():
    argv = sys.argv[1:]
    sheet = None
    if "--sheet" in argv:
        k = argv.index("--sheet")
        sheet = argv[k + 1]
        del argv[k:k + 2]
    names = argv or list(ICONS)
    OUT.mkdir(parents=True, exist_ok=True)
    files = []
    for n in names:
        if n not in ICONS:
            raise SystemExit(f"no icon {n} (have: {', '.join(ICONS)})")
        files.append(render(n, OUT))
        print("ok", n)
    if sheet:
        contact_sheet([OUT / f"{n}.png" for n in ICONS], sheet)
        print("sheet", sheet)


if __name__ == "__main__":
    main()
