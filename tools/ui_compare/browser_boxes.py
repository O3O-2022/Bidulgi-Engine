"""Measure design-system component previews in headless Edge (no extra packages).

For each component, builds a harness page (tokens as CSS variables + bundle.css + bundled fonts + the
component's preview.html), opens it with `msedge --headless --dump-dom`, and reads back the boxes of
every element that has a `data-bdg-id` attribute. A small script in the page measures them with
getBoundingClientRect() after the fonts load and writes JSON into the DOM, which --dump-dom returns.

Usage: python tools/ui_compare/browser_boxes.py [--out DIR] [--size 800x600] [Component ...]
Writes DIR/<Component>.browser.json: {"component":..., "viewport":[w,h], "boxes":[{id,x,y,w,h}, ...]}
"""
import argparse
import json
import os
import pathlib
import re
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
DS = ROOT / "design-system" / "project"
COMPONENTS = ["Panel", "Tabs", "Button", "Field", "ListRow", "Badge", "Node", "Cover"]
EDGE_CANDIDATES = [
    os.environ.get("BDG_EDGE", ""),
    r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe",
    r"C:\Program Files\Microsoft\Edge\Application\msedge.exe",
]


def token_css(tokens: dict) -> str:
    """tokens.json -> :root { --name: value; } (same names the design system uses)."""
    lines = []
    for group in ("color", "spacing", "radius", "size", "shadow"):
        for t in tokens.get(group, {}).get("tokens", []):
            lines.append(f"  --{t['name']}: {t['value']};")
    fonts = (ROOT / "Assets" / "Fonts").as_uri()
    lines.append("  --font-sans: 'Pretendard', sans-serif;")
    lines.append("  --font-mono: 'JetBrains Mono', monospace;")
    return (
        f"@font-face {{ font-family: 'Pretendard'; src: url('{fonts}/Pretendard-Regular.otf'); }}\n"
        f"@font-face {{ font-family: 'JetBrains Mono'; src: url('{fonts}/JetBrainsMono-Regular.ttf'); }}\n"
        ":root {\n" + "\n".join(lines) + "\n}\n"
    )


MEASURE = """
<script>
(async function () {
  try { await document.fonts.ready; } catch (e) {}
  const out = [];
  for (const el of document.querySelectorAll('[data-bdg-id]')) {
    const r = el.getBoundingClientRect();
    out.push({ id: el.getAttribute('data-bdg-id'), x: r.x, y: r.y, w: r.width, h: r.height });
  }
  const pre = document.createElement('pre');
  pre.id = 'bdg-boxes';
  pre.textContent = JSON.stringify(out);
  document.body.appendChild(pre);
})();
</script>
"""


def harness(component: str, tokens: dict, size: str = "800x600") -> str:
    preview = (DS / "components" / component / "preview.html").read_text(encoding="utf-8")
    bundle = (DS / "components" / "bundle.css").read_text(encoding="utf-8")
    # The engine draws text without kerning or ligatures; make the browser do the same so widths compare.
    vw = size.split("x")[0]  # headless window chrome narrows the viewport; pin the page width instead
    base = (f"html, body {{ margin: 0; width: {vw}px; background: var(--ground); overflow: hidden; }}\n"
            "* { font-kerning: none; font-variant-ligatures: none; text-rendering: optimizeSpeed; }\n")
    return ("<!doctype html><html><head><meta charset='utf-8'><style>\n" + token_css(tokens) + base + bundle +
            "\n</style></head><body>\n" + preview + MEASURE + "</body></html>\n")


def find_edge() -> str:
    for p in EDGE_CANDIDATES:
        if p and os.path.exists(p):
            return p
    sys.exit("Microsoft Edge not found (set BDG_EDGE to msedge.exe)")


def measure(edge: str, html: str, size: str) -> list:
    with tempfile.TemporaryDirectory() as tmp:
        page = pathlib.Path(tmp) / "harness.html"
        page.write_text(html, encoding="utf-8")
        w, h = size.split("x")
        cmd = [edge, "--headless", "--disable-gpu", "--no-first-run", "--allow-file-access-from-files",
               f"--user-data-dir={tmp}\\profile", f"--window-size={w},{h}", "--force-device-scale-factor=1",
               "--virtual-time-budget=5000", "--dump-dom", page.as_uri()]
        dom = subprocess.run(cmd, capture_output=True, timeout=120).stdout.decode("utf-8", "replace")
    m = re.search(r'<pre id="bdg-boxes">(.*?)</pre>', dom, re.S)
    if not m:
        sys.exit("measurement script did not run (no #bdg-boxes in the dumped DOM)")
    text = m.group(1).replace("&quot;", '"').replace("&lt;", "<").replace("&gt;", ">").replace("&amp;", "&")
    return json.loads(text)


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("components", nargs="*", default=COMPONENTS)
    ap.add_argument("--out", default=str(ROOT / "build" / "ui_compare"))
    ap.add_argument("--size", default="800x600")
    args = ap.parse_args()
    tokens = json.loads((DS / "tokens.json").read_text(encoding="utf-8"))
    edge = find_edge()
    out = pathlib.Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    for c in args.components:
        boxes = measure(edge, harness(c, tokens, args.size), args.size)
        w, h = (int(v) for v in args.size.split("x"))
        (out / f"{c}.browser.json").write_text(json.dumps({"component": c, "viewport": [w, h], "boxes": boxes}, indent=1),
                                               encoding="utf-8")
        print(f"{c}: {len(boxes)} boxes")


if __name__ == "__main__":
    main()
