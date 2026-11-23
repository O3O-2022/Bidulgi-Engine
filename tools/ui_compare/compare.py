"""Design parity check (queue 0-10): design-system HTML previews <-> engine widgets.

1. tokens : tokens.json values == values the engine loaded (BidulgiEditor --dump-tokens)
2. layout : browser boxes of [data-bdg-id] elements (browser_boxes.py, headless Edge)
            == engine widget boxes with the same id (BDG_SCREEN=parity:<Component>, BDG_DUMP_WIDGETS)

Usage: python tools/ui_compare/compare.py [--exe build/Release/BidulgiEditor.exe] [--no-browser] [--tolerance 1.0]
Prints a text report; exit code 1 when a token differs or a box is off by more than the tolerance.
"""
import argparse
import json
import os
import pathlib
import re
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))
import browser_boxes  # noqa: E402

SIZE = "800x600"


def parse_color(s: str):
    s = s.strip()
    if s.startswith("#"):
        v = s[1:]
        if len(v) == 6:
            return [int(v[i:i + 2], 16) / 255 for i in (0, 2, 4)] + [1.0]
        if len(v) == 8:
            return [int(v[i:i + 2], 16) / 255 for i in (0, 2, 4, 6)]
    m = re.match(r"rgba?\(([^)]*)\)", s)
    if m:
        parts = [float(p) for p in m.group(1).split(",")]
        return [parts[0] / 255, parts[1] / 255, parts[2] / 255, parts[3] if len(parts) > 3 else 1.0]
    raise ValueError(s)


def px(s: str) -> float:
    return float(str(s).replace("px", ""))


def expected_tokens(tokens: dict) -> dict:
    exp = {"colors": {}, "numbers": {}, "fontSizes": {}, "shadows": {}}
    for t in tokens["color"]["tokens"]:
        exp["colors"][t["name"]] = parse_color(t["value"])
    for g in ("spacing", "radius", "size"):
        for t in tokens[g]["tokens"]:
            exp["numbers"][t["name"]] = px(t["value"])
    for g in tokens["type"]["groups"]:
        for st in g["styles"]:
            exp["fontSizes"][st["name"]] = px(st["fontSize"])
    for t in tokens.get("shadow", {}).get("tokens", []):
        m = re.match(r"\s*(-?[\d.]+)(?:px)?\s+(-?[\d.]+)(?:px)?\s+([\d.]+)(?:px)?\s+(.*)$", t["value"])
        x, y, blur, color = m.groups()
        exp["shadows"][t["name"]] = {"x": float(x), "y": float(y), "blur": float(blur), "color": parse_color(color)}
    return exp


def compare_tokens(exp: dict, got: dict) -> list:
    """Returns a list of mismatch strings (empty = all equal)."""
    bad = []
    close = lambda a, b, eps: abs(a - b) <= eps  # noqa: E731
    for name, c in exp["colors"].items():
        g = got["colors"].get(name)
        if g is None or not all(close(a, b, 0.5 / 255) for a, b in zip(c, g)):
            bad.append(f"color {name}: tokens {c} engine {g}")
    for group in ("numbers", "fontSizes"):
        for name, v in exp[group].items():
            g = got[group].get(name)
            if g is None or not close(v, g, 1e-4):
                bad.append(f"{group} {name}: tokens {v} engine {g}")
    for name, s in exp["shadows"].items():
        g = got["shadows"].get(name)
        if g is None or any(not close(s[k], g[k], 1e-4) for k in ("x", "y", "blur")) or \
                not all(close(a, b, 0.5 / 255) for a, b in zip(s["color"], g["color"])):
            bad.append(f"shadow {name}: tokens {s} engine {g}")
    return bad


def run_engine(exe: str, env_extra: dict, args: list) -> None:
    env = dict(os.environ, **env_extra)
    subprocess.run([exe, "--frames", "3", "--size", SIZE] + args, env=env, cwd=ROOT, timeout=120, check=True)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("components", nargs="*", default=browser_boxes.COMPONENTS)
    ap.add_argument("--exe", default=str(ROOT / "build" / "Release" / "BidulgiEditor.exe"))
    ap.add_argument("--out", default=str(ROOT / "build" / "ui_compare"))
    ap.add_argument("--no-browser", action="store_true", help="reuse <out>/<Component>.browser.json")
    ap.add_argument("--tolerance", type=float, default=1.0)
    ap.add_argument("-v", "--verbose", action="store_true", help="list every box")
    args = ap.parse_args()
    out = pathlib.Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    failed = False

    # 1) tokens
    tokens = json.loads((browser_boxes.DS / "tokens.json").read_text(encoding="utf-8"))
    dump = out / "engine.tokens.json"
    run_engine(args.exe, {}, ["--dump-tokens", str(dump)])
    exp = expected_tokens(tokens)
    bad = compare_tokens(exp, json.loads(dump.read_text(encoding="utf-8")))
    total = sum(len(v) for v in exp.values())
    print(f"tokens   {'✓' if not bad else '✗'} {total - len(bad)}/{total}")
    for b in bad:
        print(f"  ✗ {b}")
    failed |= bool(bad)

    # 2) layout
    if not args.no_browser:
        edge = browser_boxes.find_edge()
    worst_all = 0.0
    for c in args.components:
        bfile = out / f"{c}.browser.json"
        if not args.no_browser:
            boxes = browser_boxes.measure(edge, browser_boxes.harness(c, tokens, SIZE), SIZE)
            bfile.write_text(json.dumps({"component": c, "boxes": boxes}, indent=1), encoding="utf-8")
        browser = {b["id"]: b for b in json.loads(bfile.read_text(encoding="utf-8"))["boxes"]}
        efile = out / f"{c}.engine.jsonl"
        run_engine(args.exe, {"BDG_SCREEN": f"parity:{c}", "BDG_DUMP_WIDGETS": str(efile)}, [])
        engine = {}
        for line in efile.read_text(encoding="utf-8").splitlines():
            w = json.loads(line)
            if w["id"]:
                engine[w["id"]] = w
        worst, rows, missing = 0.0, [], []
        for bid, b in browser.items():
            e = engine.get(bid)
            if not e:
                missing.append(bid)
                continue
            err = max(abs(b[k] - e[k]) for k in ("x", "y", "w", "h"))
            worst = max(worst, err)
            rows.append((err, bid, b, e))
        ok = worst <= args.tolerance and not missing
        failed |= not ok
        worst_all = max(worst_all, worst)
        print(f"layout   {'✓' if ok else '✗'} {c:<8} {len(rows)} boxes, max error {worst:.2f}px")
        for err, bid, b, e in sorted(rows, reverse=True):
            if err > args.tolerance or args.verbose:
                print(f"  {'✗' if err > args.tolerance else ' '} {bid:<22} err {err:6.2f}  browser "
                      f"{b['x']:.1f},{b['y']:.1f} {b['w']:.1f}x{b['h']:.1f}  engine {e['x']:.1f},{e['y']:.1f} {e['w']:.1f}x{e['h']:.1f}")
        for bid in missing:
            print(f"  ✗ {bid:<22} missing in the engine screen")
    print(f"result   {'FAIL' if failed else 'PASS'} (layout max error {worst_all:.2f}px, tolerance {args.tolerance}px)")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
