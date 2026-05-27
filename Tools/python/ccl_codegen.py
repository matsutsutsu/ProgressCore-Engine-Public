#!/usr/bin/env python3
# ccl_codegen.py  --  CCL Component Code Generator v3
#
# 使い方:
#   python ccl_codegen.py --src Source --out Source\_generated --include-root Source
#
# VS2022 ビルド前イベント (EngineCore.props の PreBuildEvent):
#   python "$(SolutionDir)Tools\python\ccl_codegen.py"
#          --src "$(SolutionDir)Source"
#          --out "$(SolutionDir)Source\_generated"
#          --include-root "$(SolutionDir)Source"
#
# タグ一覧 (CCL_COMPONENT の第2引数以降):
#   (なし)             全自動: Fields()/CustomGui不要/REGISTER を生成
#   "CustomGui"        REGISTER のみ生成。ComponentMeta<T>は手書き
#   "CustomGuiOnly"    Fields()は自動生成。CustomGui()の宣言だけ生成(定義は別.cpp)
#   "CustomSerialize"  REGISTER のみ生成。Serialize/Deserialize特化は手書き
#   "NoSerialize"      Fields()空で REGISTER のみ (タグコンポーネント用)

from __future__ import annotations
import argparse, re, sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import List, Optional, Tuple, Dict

# -----------------------------------------------------------------------
@dataclass
class FieldInfo:
    name: str
    cpp_type: str
    display: str
    category: str
    kind: str = 'auto'
    extra: tuple = ()

@dataclass
class ComponentInfo:
    type_name: str
    display_name: str
    header_path: Path
    fields: List[FieldInfo] = field(default_factory=list)
    # タグ
    has_custom_gui: bool       = False   # "CustomGui"        → REGISTER のみ
    has_custom_gui_only: bool  = False   # "CustomGuiOnly"    → Fields自動, CustomGui宣言のみ
    has_custom_serialize: bool = False   # "CustomSerialize"  → REGISTER のみ
    no_serialize: bool         = False   # "NoSerialize"      → Fields空

# -----------------------------------------------------------------------
class HeaderParser:
    _RE_COMPONENT = re.compile(
        r'CCL_COMPONENT\s*\(\s*"([^"]+)"\s*((?:,\s*"[^"]*"\s*)*)\)\s*'
        r'(?:struct|class)\s+(\w+)\s*[\{:]',
        re.MULTILINE)
    _RE_FIELD = re.compile(
        r'(CCL_FIELD(?:_FLOAT_RANGED|_INT_RANGED|_ENUM_U8|_ENUM_INT|_ENTITY_ID)?)'
        r'\s*\(\s*([^)]*)\)\s*([^;{}\n]+);',
        re.MULTILINE)

    def parse_file(self, path: Path) -> List[ComponentInfo]:
        try:    content = path.read_text(encoding='utf-8', errors='replace')
        except OSError as e: print(f'[ccl] WARN: {path}: {e}', file=sys.stderr); return []
        content = self._strip_comments(content)
        results: List[ComponentInfo] = []
        for m in self._RE_COMPONENT.finditer(content):
            display_name = m.group(1)
            tags         = re.findall(r'"([^"]+)"', m.group(2) or '')
            type_name    = m.group(3)
            brace_pos    = content.find('{', m.start())
            if brace_pos == -1: continue
            brace_end = self._find_matching_brace(content, brace_pos)
            if brace_end is None:
                print(f'[ccl] WARN: unmatched brace {type_name}', file=sys.stderr); continue
            body = content[brace_pos+1:brace_end]
            comp = ComponentInfo(
                type_name=type_name, display_name=display_name, header_path=path,
                has_custom_gui      = 'CustomGui'        in tags and 'CustomGuiOnly' not in tags,
                has_custom_gui_only = 'CustomGuiOnly'    in tags,
                has_custom_serialize= 'CustomSerialize'  in tags,
                no_serialize        = 'NoSerialize'      in tags,
            )
            comp.fields = self._parse_fields(body)
            results.append(comp)
        return results

    @staticmethod
    def _strip_comments(s: str) -> str:
        ph: list = []
        def stash(m): ph.append(m.group(0)); return f'\x00S{len(ph)-1}\x00'
        s = re.sub(r'"(?:\\.|[^"\\])*"', stash, s)
        s = re.sub(r'/\*.*?\*/', ' ', s, flags=re.DOTALL)
        s = re.sub(r'//[^\n]*', ' ', s)
        def restore(m): return ph[int(m.group(1))]
        return re.sub(r'\x00S(\d+)\x00', restore, s)

    @staticmethod
    def _find_matching_brace(s: str, start: int) -> Optional[int]:
        d = 0
        for i in range(start, len(s)):
            if s[i]=='{': d+=1
            elif s[i]=='}':
                d-=1
                if d==0: return i
        return None

    def _parse_fields(self, body: str) -> List[FieldInfo]:
        out = []
        for m in self._RE_FIELD.finditer(body):
            t, n = self._parse_decl(m.group(3).strip())
            if not n: continue
            f = self._build_field(m.group(1), m.group(2), t, n)
            if f: out.append(f)
        return out

    @staticmethod
    def _parse_decl(decl: str) -> Tuple[str,str]:
        for sep in ('=','{'): 
            if sep in decl: decl = decl.split(sep,1)[0]
        decl = re.sub(r'\[[^\]]*\]','',decl)
        decl = ' '.join(decl.split()).strip()
        if ' ' not in decl: return '',''
        tp, nm = decl.rsplit(' ',1)
        return tp.strip(), nm.strip(' *&,;')

    @staticmethod
    def _split_args(s: str) -> List[str]:
        res,cur,iq,d = [],[],False,0
        for c in s:
            if c=='"': iq=not iq; cur.append(c)
            elif c in '({[' and not iq: d+=1; cur.append(c)
            elif c in ')}]' and not iq: d-=1; cur.append(c)
            elif c==',' and not iq and d==0: res.append(''.join(cur).strip()); cur=[]
            else: cur.append(c)
        if cur: res.append(''.join(cur).strip())
        return res

    @classmethod
    def _build_field(cls,macro,args_str,cpp_type,name) -> Optional[FieldInfo]:
        a = cls._split_args(args_str)
        def u(s): s=s.strip(); return s[1:-1] if len(s)>=2 and s[0]=='"' and s[-1]=='"' else s
        if macro=='CCL_FIELD':
            if len(a)<2: return None
            return FieldInfo(name,cpp_type,u(a[0]),u(a[1]))
        if macro=='CCL_FIELD_FLOAT_RANGED':
            if len(a)<5: return None
            return FieldInfo(name,cpp_type,u(a[0]),u(a[4]),'float_ranged',(a[1],a[2],a[3]))
        if macro=='CCL_FIELD_INT_RANGED':
            if len(a)<4: return None
            return FieldInfo(name,cpp_type,u(a[0]),u(a[3]),'int_ranged',(a[1],a[2]))
        if macro=='CCL_FIELD_ENUM_U8':
            if len(a)<4: return None
            return FieldInfo(name,cpp_type,u(a[0]),u(a[3]),'enum_u8',(a[1],a[2]))
        if macro=='CCL_FIELD_ENUM_INT':
            if len(a)<4: return None
            return FieldInfo(name,cpp_type,u(a[0]),u(a[3]),'enum_int',(a[1],a[2]))
        if macro=='CCL_FIELD_ENTITY_ID':
            if len(a)<2: return None
            return FieldInfo(name,cpp_type,u(a[0]),u(a[1]),'entity_id')
        return None

# -----------------------------------------------------------------------
class MetaWriter:
    def emit_header_group(self, header_path: Path, comps: List[ComponentInfo],
                          include_path: str) -> str:
        L: List[str] = []
        L.append('// AUTO-GENERATED by tools/ccl_codegen.py -- DO NOT EDIT MANUALLY')
        L.append(f'// Source: {str(header_path).replace(chr(92),"/")}')
        L.append('')
        L.append(f'#include "{include_path}"')
        L.append('#include "Engine/Serialization/Meta/ComponentDefine.h"')
        L.append('')
        for c in comps:
            L.extend(self._emit_component(c))
        return '\n'.join(L)

    def _emit_component(self, c: ComponentInfo) -> List[str]:
        L: List[str] = [f'// ---- {c.type_name} ----']

        if c.no_serialize:
            # タグコンポーネント: Fields 空, 登録のみ
            L += [
                f'COMPONENT_META_BEGIN({c.type_name}, "{c.display_name}")',
                f'COMPONENT_META_END({c.type_name})',
                '',
            ]

        elif c.has_custom_gui or c.has_custom_serialize:
            # 完全手書き: REGISTER のみ生成
            L += [
                f'// "CustomGui" or "CustomSerialize" tag detected.',
                f'// ComponentMeta<{c.type_name}> and REGISTER_COMPONENT calls',
                f'// MUST be hand-written in a separate .cpp file.',
                '',
            ]

        elif c.has_custom_gui_only:
            # Fields は自動生成, CustomGui だけ宣言 (定義は別 .cpp に書く)
            L += [
                f'template <> struct ComponentMeta<{c.type_name}> {{',
                f'    CCL_META_USING_THIS({c.type_name})',
                f'    static constexpr const char* displayName  = "{c.display_name}";',
                f'    static constexpr bool        hasCustomGui = true;',
                f'',
                f'    // ★ CustomGui の定義は {c.type_name}Gui.cpp に書いてください',
                f'    static bool CustomGui({c.type_name}& comp,',
                f'                          unsigned long long entityID = 0,',
                f'                          void* worldPtr = nullptr);',
                f'',
                f'    static const std::vector<FieldDescriptor>& Fields()',
                f'    {{',
                f'        static const std::vector<FieldDescriptor> fields = {{',
            ]
            for f in c.fields:
                L.append('            ' + self._emit_field(f))
            L += [
                f'        }};',
                f'        return fields;',
                f'    }}',
                f'}};',
                f'REGISTER_COMPONENT({c.type_name}, "{c.display_name}")',
                f'REGISTER_COMPONENT_EDITOR_GUI({c.type_name})',
                '',
            ]

        else:
            # 全自動: COMPONENT_META_BEGIN/END
            L.append(f'COMPONENT_META_BEGIN({c.type_name}, "{c.display_name}")')
            for f in c.fields:
                L.append('    ' + self._emit_field(f))
            L += [
                f'COMPONENT_META_END({c.type_name})',
                '',
            ]

        return L

    @staticmethod
    def _emit_field(f: FieldInfo) -> str:
        d,c = f.display.replace('"','\\"'), f.category.replace('"','\\"')
        if f.kind=='auto':          return f'CFIELD({f.name}, "{d}", "{c}")'
        if f.kind=='float_ranged':  sp,mn,mx=f.extra; return f'CFIELD_FLOAT_RANGED({f.name}, "{d}", {sp}, {mn}, {mx}, "{c}")'
        if f.kind=='int_ranged':    mn,mx=f.extra; return f'CFIELD_INT_RANGED({f.name}, "{d}", {mn}, {mx}, "{c}")'
        if f.kind=='enum_u8':       na,co=f.extra; return f'CFIELD_ENUM_U8({f.name}, "{d}", {na}, {co}, "{c}")'
        if f.kind=='enum_int':      na,co=f.extra; return f'CFIELD_ENUM_INT({f.name}, "{d}", {na}, {co}, "{c}")'
        if f.kind=='entity_id':     return f'CFIELD_ENTITY_ID({f.name}, "{d}", "{c}")'
        return f'// unknown: {f.name}'

# -----------------------------------------------------------------------
def write_if_changed(path: Path, content: str) -> bool:
    if path.exists():
        try:
            if path.read_text(encoding='utf-8') == content: return False
        except OSError: pass
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding='utf-8')
    return True

def gen_umbrella(umbrella_path: Path, entries: List[str]) -> bool:
    lines = [
        '// AUTO-GENERATED umbrella -- VS プロジェクトにはこのファイルだけ追加する',
        '// Do not edit manually.',
        '',
    ]
    for e in sorted(entries):
        lines.append(f'#include "{e}"')
    return write_if_changed(umbrella_path, '\n'.join(lines) + '\n')

# -----------------------------------------------------------------------
def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('--src',          required=True)
    ap.add_argument('--out',          required=True, help='umbrella 出力先')
    ap.add_argument('--include-root', default=None)
    ap.add_argument('--verbose','-v', action='store_true')
    args = ap.parse_args()

    src_root     = Path(args.src).resolve()
    umbrella_dir = Path(args.out).resolve()
    inc_root     = Path(args.include_root).resolve() if args.include_root else src_root

    parser = HeaderParser()
    writer = MetaWriter()

    by_header: Dict[Path, List[ComponentInfo]] = {}
    for h in src_root.rglob('*.h'):
        try: h.relative_to(umbrella_dir); continue
        except ValueError: pass
        comps = parser.parse_file(h)
        if comps:
            by_header[h] = comps

    all_comps = [c for cs in by_header.values() for c in cs]
    if args.verbose:
        print(f'[ccl] {len(by_header)} headers / {len(all_comps)} components found')

    # 重複チェック
    seen_d, seen_t = {}, {}
    for c in all_comps:
        if c.display_name in seen_d:
            print(f'[ccl] ERROR: duplicate displayName "{c.display_name}"', file=sys.stderr)
            return 1
        seen_d[c.display_name] = c
        if c.type_name in seen_t:
            print(f'[ccl] ERROR: duplicate typeName "{c.type_name}"', file=sys.stderr)
            return 1
        seen_t[c.type_name] = c

    changed = 0
    umbrella_entries: List[str] = []

    for header_path, comps in by_header.items():
        gen_path = header_path.parent / (header_path.stem + 'Meta.generated.cpp')
        try:    rel_gen  = str(gen_path.relative_to(inc_root)).replace('\\','/')
        except: rel_gen  = str(gen_path).replace('\\','/')
        try:    rel_hdr  = str(header_path.relative_to(inc_root)).replace('\\','/')
        except: rel_hdr  = str(header_path).replace('\\','/')
        umbrella_entries.append(rel_gen)
        content = writer.emit_header_group(header_path, comps, rel_hdr)
        if write_if_changed(gen_path, content):
            changed += 1
            if args.verbose: print(f'[ccl]   generated {gen_path.name}  ({len(comps)} components)')

    umbrella_dir.mkdir(parents=True, exist_ok=True)
    if gen_umbrella(umbrella_dir / '_all_meta.cpp', umbrella_entries):
        changed += 1

    # 孤児の .generated.cpp を削除
    valid_gen = {h.parent / (h.stem + 'Meta.generated.cpp') for h in by_header}
    for gen in src_root.rglob('*.generated.cpp'):
        if gen not in valid_gen:
            gen.unlink()
            print(f'[ccl] removed orphan: {gen.name}')

    print(f'[ccl] done. {len(all_comps)} components, {changed} file(s) updated.')
    return 0

if __name__ == '__main__':
    sys.exit(main())
