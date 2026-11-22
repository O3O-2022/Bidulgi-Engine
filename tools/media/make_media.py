"""Builds README media: engine screenshots, a PhotoCraft banner and a FilmCraft intro video (GPU export).

    python tools/media/make_media.py [shots] [banner] [video]   (default: all three)

Needs: Release build of BidulgiEditor, PhotoCraft + FilmCraft installed, Pillow,
and the filmcraft-gpu skill (https://github.com/O3O-2022/filmcraft-gpu) for the export.
Outputs go to docs-media/.
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(ROOT, 'docs-media')
SHOTS = os.path.join(OUT, 'shots')
EXE = os.path.join(ROOT, 'build', 'Release', 'BidulgiEditor.exe')
PHOTOCRAFT = r'C:\Program Files\PhotoCraft\photocraft-cli.exe'
FILMCRAFT = r'C:\Program Files\FilmCraft\filmcraft-cli.exe'
FCGPU = os.path.expanduser(r'~\.claude\skills\filmcraft-gpu\scripts\fcgpu.py')
TICKS = 254016000000  # FilmCraft ticks per second
FONT = 'Malgun Gothic'


# ---------------------------------------------------------------- shots
def capture(name, **env):
    e = dict(os.environ, BDG_UI_SCALE='1.5', BDG_SCREENSHOT=os.path.join(tempfile.gettempdir(), f'bdg_{name}.png'), **env)
    subprocess.run([EXE, '--frames', '12'], cwd=ROOT, env=e, check=True)
    from PIL import Image
    Image.open(e['BDG_SCREENSHOT']).convert('RGB').save(os.path.join(SHOTS, f'{name}.png'), optimize=True)
    print('shot', name)


def shots():
    os.makedirs(SHOTS, exist_ok=True)
    capture('editor')
    capture('gallery', BDG_SCREEN='gallery')
    capture('ime', BDG_SCREEN='gallery', BDG_UI_SELFTEST='1')
    capture('stress', BDG_BATCH_STRESS='1')
    tokens = os.path.join(tempfile.gettempdir(), 'bdg_tokens_blue.json')
    src = open(os.path.join(ROOT, 'design-system', 'project', 'tokens.json'), encoding='utf-8').read()
    open(tokens, 'w', encoding='utf-8').write(src.replace('#e9a23b', '#4ea1ff').replace('#2c4a70', '#5a3d1e'))
    capture('theme', BDG_TOKENS=tokens)


# ---------------------------------------------------------------- banner (PhotoCraft)
def banner():
    out = os.path.join(OUT, 'banner.png')
    cmds = [
        ('gradient.fill.create', {'from': [0, 0], 'to': [1600, 520], 'style': 'linear',
                                  'stops': [[0, '#0b0d10'], [0.6, '#14171b'], [1, '#2a1d0b']]}),
        ('file.placeEmbedded', {'path': os.path.join(SHOTS, 'editor.png'), 'scale': 44, 'center': [1150, 270]}),
        ('layer.layerStyle.dropShadow', {'color': '#000000', 'opacity': 70, 'distance': 14, 'size': 40, 'angle': 120}),
        ('type.create', {'x': 80, 'y': 215, 'text': 'Bidulgi Engine', 'font': FONT, 'size': 68, 'color': '#d6dae0'}),
        ('type.create', {'x': 84, 'y': 275, 'text': '비둘기 엔진', 'font': FONT, 'size': 30, 'color': '#e9a23b'}),
        ('type.create', {'x': 84, 'y': 340, 'text': 'AI 친화적 · 텍스트 기반 범용 게임 엔진', 'font': FONT, 'size': 22, 'color': '#8b929c'}),
        ('type.create', {'x': 84, 'y': 380, 'text': 'C# · C++ · Graph · Lua · Python   |   MIT', 'font': FONT, 'size': 18, 'color': '#5f6670'}),
    ]
    args = [PHOTOCRAFT, 'run', '--new', json.dumps({'width': 1600, 'height': 520, 'background': '#0b0d10', 'name': 'banner'})]
    for cid, params in cmds:
        args += ['--cmd', cid, '--params', json.dumps(params, ensure_ascii=False)]
    args += ['--out', out]
    r = subprocess.run(args, capture_output=True, text=True, encoding='utf-8')
    print(r.stdout[-1500:], r.stderr[-800:])
    if r.returncode != 0 or not os.path.exists(out):
        raise SystemExit('banner failed')
    print('banner', out)


# ---------------------------------------------------------------- video (FilmCraft)
class FilmCraft:
    """Minimal driver for filmcraft-cli mcp (JSON-RPC over stdio)."""

    def __init__(self):
        self.p = subprocess.Popen([FILMCRAFT, 'mcp'], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                  stderr=subprocess.DEVNULL, text=True, encoding='utf-8', bufsize=1)
        self.n = 0
        self._rpc('initialize', {'protocolVersion': '2024-11-05', 'capabilities': {}, 'clientInfo': {'name': 'bidulgi', 'version': '1'}})
        self._send({'jsonrpc': '2.0', 'method': 'notifications/initialized'})

    def _send(self, m):
        self.p.stdin.write(json.dumps(m) + '\n')
        self.p.stdin.flush()

    def _rpc(self, method, params):
        self.n += 1
        rid = self.n
        self._send({'jsonrpc': '2.0', 'id': rid, 'method': method, 'params': params})
        while True:
            line = self.p.stdout.readline()
            if not line:
                raise RuntimeError('FilmCraft exited')
            m = json.loads(line)
            if m.get('id') == rid:
                if 'error' in m:
                    raise RuntimeError(f"{method}: {m['error']}")
                return m['result']

    def tool(self, name, args=None):
        r = self._rpc('tools/call', {'name': name, 'arguments': args or {}})
        texts = [c.get('text', '') for c in r.get('content', []) if c.get('type') == 'text']
        out = texts[0] if texts else ''
        if r.get('isError'):
            raise RuntimeError(f'{name}: {out[:400]}')
        try:
            return json.loads(out)
        except Exception:
            return out

    def run(self, cid, **params):
        return self.tool('command_run', {'id': cid, 'params': params})

    def close(self):
        try:
            self.p.stdin.close()
            self.p.wait(10)
        except Exception:
            self.p.kill()


def T(sec):
    return int(round(sec * TICKS))


SCENES = [  # (shot, seconds, caption)
    ('editor', 4.0, '유니티처럼 쉬운 에디터 · 자체 UI 프레임워크'),
    ('gallery', 3.5, '디자인 시스템 부품 8개 · 키보드 포커스'),
    ('ime', 3.5, '한글 IME 1급 지원 · 조합 중 글자까지'),
    ('theme', 3.5, 'tokens.json을 바꾸면 실행 중 바로 반영'),
    ('stress', 3.0, '사각형 1만 개 = 드로우콜 1번'),
]


def video():
    fc = FilmCraft()
    W, H, FPS = 1920, 1080, 30
    try:
        fc.run('file.newSequence', name='Bidulgi Intro', width=W, height=H, fps=FPS, video=6, audio=1)
        items = {}

        def item(path):
            if path not in items:
                r = fc.tool('media_import', {'paths': [path]})
                if r.get('errors'):
                    raise RuntimeError(r['errors'])
                items[path] = r['items'][0]
            return items[path]

        def text(at, dur, s, pos, size, color, track, style='Bold'):
            r = fc.run('graphics.newText', text=s, position=list(pos), size=size, seconds=dur, time=T(at), track=track)
            clip = r['clip']
            fc.run('graphics.set', clip=clip, layer=0, props={'font': FONT, 'font_style': style, 'size': size,
                                                              'align': 'center', 'fill_color': color, 'position': list(pos)})
            fc.run('sequence.applyVideoTransition', clip=clip, effect='cross_dissolve', frames=8, edge='in')
            fc.run('sequence.applyVideoTransition', clip=clip, effect='cross_dissolve', frames=8, edge='out')
            return clip

        intro, outro = 3.0, 3.0
        total = intro + sum(s[1] for s in SCENES) + outro
        bg = fc.run('graphics.newRectangle', position=[W / 2, H / 2], size=[W, H], seconds=total, time=0, track=0)
        fc.run('graphics.set', clip=bg['clip'], layer=0, props={'fill_color': '#0b0d10'})

        # Intro card
        text(0.3, intro - 0.3, 'Bidulgi Engine', (W / 2, H / 2 - 50), 120, '#d6dae0', 2)
        text(0.6, intro - 0.6, 'AI 친화적 · 텍스트 기반 범용 게임 엔진', (W / 2, H / 2 + 70), 44, '#e9a23b', 3, 'Regular')

        t = intro
        for name, dur, caption in SCENES:
            r = fc.run('timeline.place', item=item(os.path.join(SHOTS, f'{name}.png')), track='V2', time=T(t),
                       sourceIn=0, duration=T(dur), audio=False, video=True)
            (clip,) = r['clips']
            fc.run('effects.setParam', clip=clip, effect='motion', param='scale', value=88)
            fc.run('effects.setParam', clip=clip, effect='motion', param='position', value=[W / 2, H / 2 - 40])
            fc.run('sequence.applyVideoTransition', clip=clip, effect='cross_dissolve', frames=10, edge='in')
            fc.run('sequence.applyVideoTransition', clip=clip, effect='cross_dissolve', frames=10, edge='out')
            text(t + 0.2, dur - 0.2, caption, (W / 2, H - 52), 40, '#d6dae0', 3)
            t += dur

        # Outro card
        text(t + 0.2, outro - 0.2, '비둘기 엔진', (W / 2, H / 2 - 40), 96, '#e9a23b', 2)
        text(t + 0.4, outro - 0.4, 'MIT License · pre-alpha (M0)', (W / 2, H / 2 + 70), 40, '#8b929c', 3, 'Regular')

        proj = os.path.join(OUT, 'bidulgi-intro.fcproj')
        fc.run('file.saveAs', path=proj)
        for sec in (1.5, intro + 2, intro + 9, total - 1.5):
            fc.run('file.exportFrame', path=os.path.join(tempfile.gettempdir(), f'bdg_frame_{sec:05.2f}.png'),
                   format='png', target='program', time=T(sec))
        print('project', proj, 'seconds', total)
    finally:
        fc.close()

    # GPU export through the filmcraft-gpu skill (renderer=gpu, NVENC, VBR, verified with probe).
    mp4 = os.path.join(OUT, 'bidulgi-intro.mp4')
    r = subprocess.run([sys.executable, FCGPU, 'export', mp4, '--project', proj], text=True, encoding='utf-8')
    if r.returncode != 0:
        raise SystemExit(f'export failed ({r.returncode})')

    # Animated GIF for the README (GitHub does not inline mp4 in README files).
    from PIL import Image
    frames = []
    for name, dur, _ in SCENES:
        im = Image.open(os.path.join(SHOTS, f'{name}.png')).convert('RGB').resize((960, 540), Image.LANCZOS)
        frames.append((im.quantize(colors=128, method=Image.Quantize.MEDIANCUT), int(dur * 1000)))
    gif = os.path.join(OUT, 'bidulgi-intro.gif')
    frames[0][0].save(gif, save_all=True, append_images=[f for f, _ in frames[1:]], duration=[d for _, d in frames],
                      loop=0, optimize=True)
    print('gif', gif, os.path.getsize(gif) // 1024, 'KB')


if __name__ == '__main__':
    steps = sys.argv[1:] or ['shots', 'banner', 'video']
    os.makedirs(OUT, exist_ok=True)
    for s in steps:
        {'shots': shots, 'banner': banner, 'video': video}[s]()
