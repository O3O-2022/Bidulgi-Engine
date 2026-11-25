"""AI-2: writes the API reference an AI agent (or a person) reads first.

  Docs/llms.txt          short index: what the engine is, how to drive it, where everything is
  Docs/api/components.md every scene component with its fields, types and defaults (from `bdg schema`)
  Docs/api/cli.md        the `bdg` commands (from `bdg help`)
  Docs/api/csharp.md     the public C# game API (Scripting/Bidulgi.Scripting/*.cs), with the comments next to it

Usage: python tools/gen_llms.py [--bdg build/Release/bdg.exe] [--check]
--check writes nothing and exits 1 when a file would change (CI keeps the reference up to date).
"""
import io
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def run_bdg(bdg, *args):
    r = subprocess.run([bdg, *args], capture_output=True, cwd=ROOT)
    return r.stdout.decode('utf-8', 'replace').replace('\r\n', '\n')


def components(bdg):
    lines = [l for l in run_bdg(bdg, 'schema').split('\n') if ':' in l]
    out = ['# 컴포넌트 레퍼런스', '',
           '장면 파일(`.scene`, 한 줄에 물체 하나)에 쓰는 컴포넌트와 필드입니다. `bdg schema <이름>`과 같은 내용이고,',
           '`bdg set <id> 컴포넌트.필드=값`, `bdg addcomp <id> <컴포넌트>`로 바꿉니다. 이 파일은 `tools/gen_llms.py`가 만듭니다.', '']
    for l in sorted(lines, key=lambda x: x.split(':', 1)[0]):
        name, rest = l.split(':', 1)
        out += [f'## {name}', '', '| 필드 | 타입 | 기본값 |', '|---|---|---|']
        for f in rest.split():
            m = re.match(r'([^:]+):([^=]+)(?:=(.*))?$', f)
            if m:
                out.append(f'| `{m.group(1)}` | {m.group(2)} | {("`" + m.group(3) + "`") if m.group(3) is not None else "—"} |')
        out.append('')
    return '\n'.join(out), len(lines)


def cli(bdg):
    text = run_bdg(bdg, 'help').rstrip()
    return '\n'.join(['# bdg 명령줄', '', '에디터와 같은 명령 버스로 장면을 읽고 바꿉니다(실행 취소 기록에 남음). 모든 명령에 `--json`을 붙이면',
                      '결과와 오류가 JSON 한 개로 나옵니다: `{"ok", "exit", "command", "output": [...], "error": {"code", "message", "hint"}}`.',
                      '', '```', text, '```', '']), text.count('\n  ')


DECL = re.compile(r'^\s*(\[[^\]]*\]\s*)*public\s+(?!.*\boverride\b)')


def csharp():
    src = os.path.join(ROOT, 'Scripting', 'Bidulgi.Scripting')
    out = ['# C# 게임 API', '',
           '게임 스크립트(`Behaviour`를 상속한 클래스)에서 쓰는 공개 API입니다. 이름은 유니티와 비슷하게 맞췄습니다.',
           '이 파일은 `tools/gen_llms.py`가 `Scripting/Bidulgi.Scripting/*.cs`에서 만듭니다(주석 = 설명).', '']
    count = 0
    for fn in sorted(os.listdir(src)):
        if not fn.endswith('.cs'):
            continue
        lines = io.open(os.path.join(src, fn), encoding='utf-8').read().split('\n')
        items = []
        depth = 0
        for i, l in enumerate(lines):
            code = l.split('//', 1)[0]
            if DECL.match(l) and depth <= 2:
                sig = re.sub(r'^\s*(\[[^\]]*\]\s*)*', '', code).strip()
                k = i + 1
                while sig.count('(') > sig.count(')') and k < len(lines):  # a signature over several lines
                    sig += ' ' + lines[k].split('//', 1)[0].strip()
                    k += 1
                sig = re.sub(r'\s*(=>.*|\{.*)$', '', sig).rstrip(';').strip()
                if not sig:
                    continue
                note = l.split('//', 1)[1].strip() if '//' in l else ''
                j, above = i - 1, []
                while j >= 0 and lines[j].strip().startswith('//'):
                    above.insert(0, lines[j].strip().lstrip('/').strip())
                    j -= 1
                desc = ' '.join(above + ([note] if note else []))
                is_type = re.search(r'\b(class|struct|enum|interface|record)\b', sig) is not None
                items.append((is_type, sig, desc))
            depth += code.count('{') - code.count('}')
        if not items:
            continue
        out += [f'## {fn}', '']
        for is_type, sig, desc in items:
            count += 1
            if is_type:
                out += ['', f'### `{sig}`', ''] + ([desc, ''] if desc else [])
            else:
                out.append(f'- `{sig}`' + (f' — {desc}' if desc else ''))
        out.append('')
    return '\n'.join(out), count


def features():
    d = os.path.join(ROOT, 'Docs', 'features')
    rows = []
    for fn in sorted(os.listdir(d)):
        if not fn.endswith('.md'):
            continue
        text = io.open(os.path.join(d, fn), encoding='utf-8').read().split('\n')
        title = next((l.lstrip('# ').strip() for l in text if l.startswith('# ')), fn)
        para = next((l.strip() for l in text[1:] if l.strip() and not l.startswith('#') and not l.startswith('|')), '')
        para = re.sub(r'\s+', ' ', para)
        if len(para) > 160:
            para = para[:157].rstrip() + '…'
        rows.append(f'- [{title}](features/{fn}): {para}')
    return rows


def llms(ncomp, ncli, ncs):
    return '\n'.join([
        '# 비둘기 엔진 (Bidulgi Engine)', '',
        '> C++20 게임 엔진과 에디터. 장면·에셋·설정이 전부 텍스트(JSONL/JSON)이고, 에디터와 같은 명령 버스를 쓰는 `bdg` 명령줄이 있어',
        '> AI 에이전트가 장면을 읽고·고치고·찍고·검사할 수 있습니다. 게임 코드는 C#(유니티와 비슷한 이름). MIT 라이선스.', '',
        '## AI가 엔진을 다루는 순서', '',
        '1. 프로젝트 만들기: `bdg new <dir> [--template fps|platformer2d|thirdperson]` (템플릿마다 `AGENTS.md`가 들어 있음)',
        '2. 장면 보기: `bdg view` / `bdg outline` / `bdg query <패턴> -f 필드` (모두 `--json` 가능)',
        '3. 구조 알기: `bdg schema [컴포넌트]` → [api/components.md](api/components.md)',
        '4. 고치기: `bdg set <id> Comp.field=값`, `bdg add`, `bdg addcomp`, `bdg del` (실행 취소 가능, `--dry-run`)',
        '5. 확인: `bdg validate`, 사진은 `bdg shot [--bookmarks] [--camera yaw,pitch,dist]`',
        '6. 게임 코드: C# `Behaviour` → [api/csharp.md](api/csharp.md)',
        '7. 빌드: `bdg build`', '',
        '## 레퍼런스', '',
        f'- [api/cli.md](api/cli.md): `bdg` 명령 {ncli}개',
        f'- [api/components.md](api/components.md): 장면 컴포넌트 {ncomp}개와 필드·타입·기본값',
        f'- [api/csharp.md](api/csharp.md): C# 공개 API {ncs}개',
        '- [QUEUE.md](QUEUE.md): 작업 큐, 빌드 방법, 자동 검사 환경 변수(`BDG_*_TEST`), 코드 지도', '',
        '## 기능 문서', ''] + features() + [''])


def main():
    bdg = os.path.join(ROOT, 'build', 'Release', 'bdg.exe')
    if '--bdg' in sys.argv:
        bdg = sys.argv[sys.argv.index('--bdg') + 1]
    check = '--check' in sys.argv
    comp, ncomp = components(bdg)
    cl, ncli = cli(bdg)
    cs, ncs = csharp()
    files = {'Docs/api/components.md': comp, 'Docs/api/cli.md': cl, 'Docs/api/csharp.md': cs, 'Docs/llms.txt': llms(ncomp, ncli, ncs)}
    stale = []
    for rel, text in files.items():
        path = os.path.join(ROOT, rel)
        old = io.open(path, encoding='utf-8').read() if os.path.exists(path) else None
        if old != text:
            stale.append(rel)
            if not check:
                os.makedirs(os.path.dirname(path), exist_ok=True)
                io.open(path, 'w', encoding='utf-8', newline='\n').write(text)
    if check and stale:
        print('out of date (run python tools/gen_llms.py):', ', '.join(stale))
        return 1
    print(f'components {ncomp}, cli {ncli}, csharp {ncs}; {"changed: " + ", ".join(stale) if stale else "up to date"}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
