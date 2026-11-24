"""Bidulgi Header Tool: reads BCLASS/BSTRUCT/BENUM/BPROPERTY/BFUNCTION markers in a C++ header and writes
<Stem>.gen.cpp with the reflection registration code (queue 1-2). No libclang: the header is tokenised
and only names are parsed; field, parameter and return types are deduced by templates in
Engine/Core/Reflection.h (decltype / member function pointers), so templates such as std::vector<T> need
no parsing here.

Usage: python bht.py <header> <out.gen.cpp> <include-as>
  <include-as>  how the generated file includes the header, e.g. "Tests/BTestComponent.h"
CMake runs it per header (bdg_reflect in CMakeLists.txt), only when that header or the tool changed.
"""
import pathlib
import re
import sys

TOKEN = re.compile(r'''
    (?P<ws>\s+) |
    (?P<comment>//[^\n]*|/\*.*?\*/) |
    (?P<pp>\#[^\n]*) |
    (?P<str>"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*') |
    (?P<id>[A-Za-z_]\w*) |
    (?P<num>\d[\w.']*) |
    (?P<op>::|->|.)
''', re.S | re.X)

PROPERTY_FLAGS = {"Edit": "::bdg::kEdit", "ReadOnly": "::bdg::kReadOnly", "VisibleOnly": "::bdg::kReadOnly",
                  "Replicated": "::bdg::kReplicated", "Animatable": "::bdg::kAnimatable"}


class ParseError(Exception):
    pass


def tokenize(text):
    out = []
    line = 1
    for m in TOKEN.finditer(text):
        kind = m.lastgroup
        value = m.group()
        if kind not in ("ws", "comment", "pp"):
            out.append((kind, value, line))
        line += value.count("\n")
    return out


class Parser:
    def __init__(self, tokens, path):
        self.t = tokens
        self.i = 0
        self.path = path

    def peek(self, k=0):
        j = self.i + k
        return self.t[j][1] if j < len(self.t) else ""

    def next(self):
        tok = self.t[self.i]
        self.i += 1
        return tok[1]

    def error(self, msg):
        line = self.t[min(self.i, len(self.t) - 1)][2] if self.t else 0
        raise ParseError(f"{self.path}({line}): {msg}")

    def expect(self, value):
        if self.peek() != value:
            self.error(f"expected '{value}', found '{self.peek()}'")
        return self.next()

    def balanced(self, open_, close):
        """Consumes from an opening bracket to its match; returns the inner tokens."""
        self.expect(open_)
        depth, inner = 1, []
        while self.i < len(self.t):
            v = self.next()
            if v == open_:
                depth += 1
            elif v == close:
                depth -= 1
                if depth == 0:
                    return inner
            inner.append(v)
        self.error(f"unbalanced '{open_}'")


def split_top(tokens, sep=","):
    """Splits a token list at top-level separators (outside (), <>, [], {})."""
    parts, cur, depth = [], [], 0
    for v in tokens:
        if v in "([{<":
            depth += 1
        elif v in ")]}>":
            depth -= 1
        if v == sep and depth == 0:
            parts.append(cur)
            cur = []
        else:
            cur.append(v)
    if cur:
        parts.append(cur)
    return parts


def parse_args(tokens):
    """Macro arguments: [Flag, Key = "value", Key = 1.5] -> (flags, {key: value})."""
    flags, kv = [], {}
    for part in split_top(tokens):
        if not part:
            continue
        if len(part) >= 3 and part[1] == "=":
            value = "".join(part[2:])
            kv[part[0]] = value[1:-1] if value.startswith('"') else value
        else:
            flags.append(part[0])
    return flags, kv


def cpp_str(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def parse(text, path):
    p = Parser(tokenize(text), path)
    namespaces = []   # stack of (name or None for other braces)
    types, enums = [], []
    while p.i < len(p.t):
        v = p.peek()
        if v == "namespace" and p.i > 0 and p.t[p.i - 1][1] == "using":  # using namespace x;
            p.next()
            continue
        if v == "namespace" and (p.peek(1) == "{" or re.match(r"[A-Za-z_]", p.peek(1) or "")):
            p.next()
            name = []
            while p.peek() not in ("{", ""):
                name.append(p.next())
            p.expect("{")
            namespaces.append("".join(name) or None)
            continue
        if v == "{":
            p.next()
            namespaces.append(None)
            continue
        if v == "}":
            p.next()
            if namespaces:
                namespaces.pop()
            continue
        if v == "BENUM":
            p.next()
            p.balanced("(", ")")
            p.expect("enum")
            if p.peek() in ("class", "struct"):
                p.next()
            name = p.next()
            if p.peek() == ":":
                while p.peek() != "{":
                    p.next()
            body = p.balanced("{", "}")
            values = [part[0] for part in split_top(body) if part]
            enums.append({"name": name, "ns": [n for n in namespaces if n], "values": values})
            continue
        if v in ("BCLASS", "BSTRUCT"):
            is_struct = p.next() == "BSTRUCT"
            flags, _ = parse_args(p.balanced("(", ")"))
            if p.peek() not in ("class", "struct"):
                p.error(f"{v}(...) must be followed by a class or struct")
            p.next()
            name = p.next()
            base = None
            while p.peek() not in ("{", ":"):
                p.next()  # final, attributes
            if p.peek() == ":":
                p.next()
                bases = []
                while p.peek() != "{":
                    bases.append(p.next())
                first = split_top(bases)[0]
                first = [b for b in first if b not in ("public", "protected", "private", "virtual")]
                base = "".join(first)
            body_start = p.i
            body = p.balanced("{", "}")
            if "GENERATED_BODY" not in body:
                p.i = body_start
                p.error(f"{name}: add GENERATED_BODY() at the top of the class body")
            types.append({"name": name, "ns": [n for n in namespaces if n], "base": base, "struct": is_struct,
                          "tags": flags, **parse_members(body, path, name)})
            continue
        p.next()
    return types, enums


def parse_members(body, path, owner):
    fields, functions = [], []
    i, depth = 0, 0
    while i < len(body):
        v = body[i]
        if v in ("{",):
            depth += 1
        elif v == "}":
            depth -= 1
        if depth == 0 and v in ("BPROPERTY", "BFUNCTION") and i + 1 < len(body) and body[i + 1] == "(":
            # macro arguments
            j, d = i + 2, 1
            while d:
                d += {"(": 1, ")": -1}.get(body[j], 0)
                j += 1
            args = parse_args(body[i + 2:j - 1])
            # declaration up to ';' or a function body '{' at top level
            k, d, decl = j, 0, []
            while k < len(body):
                t = body[k]
                if t in "([<":
                    d += 1
                elif t in ")]>":
                    d -= 1
                if d == 0 and (t == ";" or (t == "{" and v == "BFUNCTION" and decl and decl[-1] in (")", "const", "override", "noexcept"))):
                    break
                if d == 0 and t == "{" and v == "BPROPERTY":  # brace initialiser: int hp{100};
                    break
                decl.append(t)
                k += 1
            if v == "BPROPERTY":
                cut = [n for n, t in enumerate(decl) if t == "="]
                head = decl[:cut[0]] if cut else decl
                names = [t for t in head if re.match(r"[A-Za-z_]\w*$", t)]
                if not names:
                    raise ParseError(f"{path}: {owner}: cannot find the field name after BPROPERTY")
                fields.append({"name": names[-1], "flags": args[0], "meta": args[1]})
            else:
                if "(" not in decl:
                    raise ParseError(f"{path}: {owner}: BFUNCTION must be followed by a function declaration")
                lp = decl.index("(")
                name = decl[lp - 1]
                # parameter list between the first '(' and its match
                d, rp = 0, lp
                for rp in range(lp, len(decl)):
                    d += {"(": 1, ")": -1}.get(decl[rp], 0)
                    if d == 0:
                        break
                params = []
                for n, part in enumerate(split_top(decl[lp + 1:rp])):
                    if part == ["void"]:
                        continue
                    if "=" in part:
                        part = part[:part.index("=")]
                    ids = [t for t in part if re.match(r"[A-Za-z_]\w*$", t) and t not in ("const", "unsigned", "signed")]
                    params.append(ids[-1] if len(ids) >= 2 else f"arg{n}")
                functions.append({"name": name, "flags": args[0], "meta": args[1], "params": params})
            # skip a function body if present
            if k < len(body) and body[k] == "{":
                d = 0
                while k < len(body):
                    d += {"{": 1, "}": -1}.get(body[k], 0)
                    k += 1
                    if d == 0:
                        break
            i = k
            continue
        i += 1
    return {"fields": fields, "functions": functions}


def qualified(item):
    return "::" + "::".join(item["ns"] + [item["name"]])


def generate(types, enums, include, stem):
    out = [f"// Generated by tools/bht/bht.py from {include}. Do not edit.",
           f'#include "{include}"',
           '#include "Core/Reflection.h"',
           "#include <cstddef>",
           "#if defined(_MSC_VER)",
           "#pragma warning(disable : 4127) // conditional expression is constant (generated tables)",
           "#endif", ""]
    for e in enums:
        q = qualified(e)
        vals = ", ".join(f"{{{cpp_str(v)}, static_cast<int64_t>({q}::{v})}}" for v in e["values"])
        out += [f"template <> const ::bdg::EnumInfo& bdg::StaticEnum<{q}>() {{",
                f"  static const ::bdg::EnumInfo info{{{cpp_str(e['name'])}, {{{vals}}}}};",
                "  return info;", "}", ""]
    for t in types:
        q = qualified(t)
        out += [f"const ::bdg::TypeInfo& {q}::StaticType() {{",
                f"  using Self = {q};",
                "  static const ::bdg::TypeInfo info = [] {",
                "    ::bdg::TypeInfo t;",
                f"    t.name = {cpp_str(t['name'])};",
                "    t.size = sizeof(Self);",
                f"    t.isStruct = {'true' if t['struct'] else 'false'};",
                f"    t.tags = {{{', '.join(cpp_str(x) for x in t['tags'])}}};",
                "    t.create = ::bdg::reflect::Creator<Self>();",
                "    t.destroy = ::bdg::reflect::Destroyer<Self>();"]
        if t["base"]:
            out.append(f"    t.base = ::bdg::reflect::BaseTypeOf<{t['base']}>();")
        for f in t["fields"]:
            flags = ["::bdg::kSave"] if "Transient" not in f["flags"] else []
            flags += [PROPERTY_FLAGS[x] for x in f["flags"] if x in PROPERTY_FLAGS]
            unknown = [x for x in f["flags"] if x not in PROPERTY_FLAGS and x != "Transient"]
            if unknown:
                raise ParseError(f"{t['name']}.{f['name']}: unknown BPROPERTY flag(s) {', '.join(unknown)} "
                                 f"(use {', '.join(sorted(PROPERTY_FLAGS))}, Transient)")
            m = f["meta"]
            rng = m.get("Range")
            lo, hi = (rng.split(",") + ["0"])[:2] if rng else ("0", "0")
            meta = (f"::bdg::FieldMeta{{{'true' if rng else 'false'}, {lo.strip() or 0}, {hi.strip() or 0}, "
                    f"{cpp_str(m.get('Tooltip', ''))}, {cpp_str(m.get('Category', ''))}, {cpp_str(m.get('ShowIf', ''))}, "
                    f"{cpp_str(m.get('Header', ''))}}}")
            out.append(f"    t.fields.push_back(::bdg::reflect::Field<decltype(Self::{f['name']})>({cpp_str(f['name'])}, "
                       f"offsetof(Self, {f['name']}), {' | '.join(flags) or '0u'}, {meta}));")
        for fn in t["functions"]:
            names = ", ".join(cpp_str(n) for n in fn["params"])
            unknown = [x for x in fn["flags"] if x not in ("Callable", "Button", "Pure")]
            if unknown:
                raise ParseError(f"{path}: {t['name']}.{fn['name']}: unknown BFUNCTION flag {unknown[0]} (use Callable, Button, Pure)")
            fflags = ("::bdg::kCallable" + (" | ::bdg::kButton" if "Button" in fn["flags"] else "")
                      + (" | ::bdg::kPure" if "Pure" in fn["flags"] else ""))
            out.append(f"    t.functions.push_back(::bdg::reflect::Function({cpp_str(fn['name'])}, &Self::{fn['name']}, "
                       f"{fflags}, {{{names}}}));")
        out += ["    return t;", "  }();", "  return info;", "}", ""]
    out.append(f"void bdg_reflect_register_{stem}() {{")
    out.append("  auto& r = ::bdg::Registry::Instance();")
    for e in enums:
        out.append(f"  r.AddEnum(::bdg::StaticEnum<{qualified(e)}>());")
    for t in types:
        out.append(f"  r.Add({qualified(t)}::StaticType());")
    out += ["}", ""]
    return "\n".join(out)


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    header, output, include = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2]), sys.argv[3]
    stem = re.sub(r"\W", "_", header.stem)
    try:
        types, enums = parse(header.read_text(encoding="utf-8"), str(header))
        code = generate(types, enums, include, stem)
    except ParseError as e:
        sys.exit(f"bht: error: {e}")
    output.parent.mkdir(parents=True, exist_ok=True)
    if not output.exists() or output.read_text(encoding="utf-8") != code:
        output.write_text(code, encoding="utf-8", newline="\n")
    else:
        output.touch()  # newer than the header, so the build does not run the tool again


if __name__ == "__main__":
    main()
