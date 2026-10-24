#!/usr/bin/env python3
"""Writes what a sandboxed (tier 2, WebAssembly) plugin and the host agree on, from the C headers of sdk/include/shard (docs/design-notes/wasm-sandbox.fr.md, W2) :

    sdk/include/shard/shard_wasm_imports.h          the module's side : one import per function of every interface, the tables (ShardCVarsApi...)
                                                    built from them, query_interface and the ShardHostApi the host reads from the module
    src/engine/plugins/wasm/wasm_layout_gen.hpp     the wasm32 layout of every struct of the headers (pointers are 4 bytes), the permissions and
                                                    the interfaces the sandbox offers
    src/engine/plugins/wasm/wasm_imports_gen.cpp    the host's side of every function whose arguments are numbers, strings, buffers and plain
                                                    structs : it reads them from the module's memory, calls the C ABI of the host and writes the
                                                    results back. Functions that take callbacks, descriptors with pointers or hand out pointers
                                                    into host memory are written by hand (MANUAL below) : the generator checks the list is complete.

    python scripts/gen_wasm_imports.py            writes the three files
    python scripts/gen_wasm_imports.py --check    fails (exit 1) if one is not what the headers give : the test Wasm.GeneratedImports

The headers are the reference : change a header, run this, and the sandbox follows. The wasm32 layouts are computed here (ILP32 rules) and
checked against Clang's by compiling tests/plugin_fixtures/wasm_plugin/wasm_plugin.c with SHARD_WASM_CHECK_LAYOUT.
"""

import argparse
import os
import re
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
C_DIR = os.path.join(ROOT, 'sdk', 'include', 'shard')
OUT_SDK = os.path.join(C_DIR, 'shard_wasm_imports.h')
OUT_LAYOUT = os.path.join(ROOT, 'src', 'engine', 'plugins', 'wasm', 'wasm_layout_gen.hpp')
OUT_HOST = os.path.join(ROOT, 'src', 'engine', 'plugins', 'wasm', 'wasm_imports_gen.cpp')

# The interfaces a sandboxed plugin can reach (the rest of the C ABI stays out : docs/design-notes/wasm-sandbox.fr.md section 2).
# (header, table struct, short name, permission that has to be in the manifest for the interface to be offered at all)
INTERFACES = [
    ('shard_plugin.h', 'ShardPluginApi', 'plugin', None),
    ('shard_commands.h', 'ShardCommandsApi', 'commands', None),
    ('shard_cvars.h', 'ShardCVarsApi', 'cvars', None),
    ('shard_components.h', 'ShardComponentsApi', 'components', None),
    ('shard_ecs.h', 'ShardEcsApi', 'ecs', None),
    ('shard_systems.h', 'ShardSystemsApi', 'systems', None),
    ('shard_events.h', 'ShardEventsApi', 'events', None),
    ('shard_content.h', 'ShardContentApi', 'content', 'fs.read:content'),
    ('shard_state.h', 'ShardStateApi', 'state', None),
    ('shard_actors.h', 'ShardActorsApi', 'actors', None),
    ('shard_scripts.h', 'ShardScriptsApi', 'scripts', None),
    ('shard_input.h', 'ShardInputApi', 'input', 'input'),
    ('shard_physics.h', 'ShardPhysicsApi', 'physics', 'physics'),
    ('shard_audio.h', 'ShardAudioApi', 'audio', 'audio'),
    ('shard_worlds.h', 'ShardWorldsApi', 'worlds', None),
    ('shard_editor.h', 'ShardEditorApi', 'editor', 'editor'),
]
# Tables the host hands a callback of the plugin (the widgets of a panel, the gizmo of a viewport) : not interfaces one asks for, but imports too.
# The module builds them from the imports and exports them (shard_wasm_<name>_table) ; they act on what the host is drawing right now, which the
# sandbox keeps in the member of that name (Sandbox::ui, Sandbox::gizmo). (header, table struct, short name, permission)
TABLES = [
    ('shard_editor.h', 'ShardUiApi', 'ui', 'editor'),
    ('shard_editor.h', 'ShardGizmoApi', 'gizmo', 'editor'),
]
# Headers whose structs the sandbox reads or writes without being an interface of its own
EXTRA_HEADERS = ['shard_abi.h', 'shard_host.h']

# Written by hand in the host (src/engine/plugins/wasm/wasm_imports_*.cpp), with the reason
MANUAL = {
    'commands.add': 'a callback : a table index of the module',
    'components.register_type': 'a descriptor with pointers (fields, defaults)',
    'components.field_at': 'strings that live in the host, copied into the module',
    'components.find_field': 'strings that live in the host, copied into the module',
    'components.blob_data': 'a pointer into host memory : the module gets a copy that is written back',
    'components.blob_resize': 'the copy of the blob goes with its bytes',
    'components.blob_free': 'the copy of the blob goes with it',
    'components.string_get': 'a pointer into host memory',
    'ecs.each_chunk': 'a callback, and the columns are copied into the module',
    'ecs.count': 'a query with pointers',
    'ecs.get': 'a pointer into host memory : the module gets a copy that is written back',
    'systems.add': 'a descriptor with pointers and a callback',
    'events.subscribe': 'a callback',
    'events.subscribe_world': 'a callback',
    'events.describe': 'an array of descriptors with strings',
    'content.read': 'the file is copied into the module, and only its own',
    'content.list': 'a callback, and only its own files',
    'scripts.register_script': 'a descriptor with pointers and callbacks',
    'scripts.get_object': 'the object lives in the module : the host keeps a mirror of its fields',
    'scripts.add_to_actor': 'the object lives in the module : the host keeps a mirror of its fields',
    'editor.add_panel': 'a callback',
    'editor.add_menu_item': 'a callback',
    'editor.add_toolbar_button': 'a callback',
    'editor.add_inspector': 'a callback',
    'editor.add_gizmo': 'a callback',
    'editor.subscribe_selection': 'a callback',
    'ui.combo': 'an array of strings',
}
# Always answered SHARD_E_UNSUPPORTED : they would hand the module a path of the host (content.*), or they are new in ABI 1.7 and take a descriptor
# with pointers or a callback that the sandbox does not marshal yet (physics queries and colliders, the undo history, asset openers, drag and drop).
# A sandboxed plugin gets SHARD_E_UNSUPPORTED from them ; moving one to MANUAL (and writing its import) is an ordinary addition.
DENIED_ALWAYS = {'content.resolve', 'content.resolve_logical', 'ShardPhysicsApi.raycast_all', 'physics.raycast_all', 'physics.overlap_sphere', 'physics.overlap_box', 'physics.overlap_capsule', 'physics.sweep_sphere', 'physics.sweep_box', 'physics.collider_create', 'physics.collider_set_pose', 'physics.collider_update', 'editor.undo_push', 'editor.add_asset_opener', 'ui.accept_drag_drop_payload'}
# Permissions of single functions (the interface may be offered without them)
FUNCTION_PERMISSIONS = {
    'worlds.load_world': 'worlds.load',
    'actors.create': 'actors.spawn',
    'actors.spawn': 'actors.spawn',
    'worlds.spawn_prefab': 'actors.spawn',
}

# Types of the C ABI : (size, alignment, wasm value type) on wasm32 (ILP32)
SCALARS = {
    'int8_t': (1, 1, 'i'), 'uint8_t': (1, 1, 'i'), 'int16_t': (2, 2, 'i'), 'uint16_t': (2, 2, 'i'),
    'int32_t': (4, 4, 'i'), 'uint32_t': (4, 4, 'i'), 'float': (4, 4, 'f'),
    'int64_t': (8, 8, 'l'), 'uint64_t': (8, 8, 'l'), 'double': (8, 8, 'd'),
}
WASM_LETTER = {'i': 'I', 'l': 'L', 'f': 'F', 'd': 'D'}


class GenError(Exception):
    pass


# ---------------------------------------------------------------------------------------------------- parsing

class CType:
    def __init__(self, base, ptr=0, const=False, array=None):
        self.base, self.ptr, self.const, self.array = base, ptr, const, array

    def __repr__(self):
        return ('const ' if self.const else '') + self.base + '*' * self.ptr + ('[%s]' % self.array if self.array else '')


class Param:
    def __init__(self, ctype, name):
        self.type, self.name = ctype, name


class Func:
    def __init__(self, name, ret, params):
        self.name, self.ret, self.params = name, ret, params


class Struct:
    def __init__(self, name):
        self.name = name
        self.fields = []        # (name, CType)
        self.funcs = []         # for a table : Func


def strip_comments(text):
    text = re.sub(r'/\*.*?\*/', ' ', text, flags=re.S)
    text = re.sub(r'//[^\n]*', ' ', text)
    out = []
    for line in text.split('\n'):
        if re.match(r'^\s*#', line):
            continue
        out.append(line)
    return ' '.join(' '.join(out).split())


def parse_type(text):
    """'const ShardStr*' -> CType ; the name is not part of it"""
    text = text.strip()
    const = False
    words = text.replace('*', ' * ').split()
    base = None
    ptr = 0
    for w in words:
        if w == 'const':
            const = True
        elif w == '*':
            ptr += 1
        elif w in ('struct', 'volatile'):
            continue
        else:
            base = w
    if base is None:
        raise GenError('cannot read the type "%s"' % text)
    return CType(base, ptr, const)


def parse_declarator(text):
    """'const float velocity[3]' / 'ShardStr* out' / 'int32_t key' -> Param"""
    text = text.strip()
    array = None
    m = re.match(r'^(.*?)\s*\[\s*(\w+)\s*\]$', text)
    if m:
        text, array = m.group(1), m.group(2)
    m = re.match(r'^(.*?)(\w+)$', text.strip())
    if not m or not m.group(1).strip():
        raise GenError('cannot read the declaration "%s"' % text)
    ctype = parse_type(m.group(1))
    ctype.array = array
    return Param(ctype, m.group(2))


def split_top(text, sep=','):
    parts, depth, cur = [], 0, ''
    for ch in text:
        if ch in '([':
            depth += 1
        elif ch in ')]':
            depth -= 1
        if ch == sep and depth == 0:
            parts.append(cur)
            cur = ''
        else:
            cur += ch
    if cur.strip():
        parts.append(cur)
    return parts


FUNC_MEMBER = re.compile(r'^(.*?)\(\s*(?:SHARD_CALL\s*)?\*\s*(\w+)\s*\)\s*\((.*)\)$')


def parse_func(decl):
    m = FUNC_MEMBER.match(decl.strip())
    if not m:
        return None
    ret = parse_type(m.group(1))
    args = m.group(3).strip()
    params = [] if args in ('', 'void') else [parse_declarator(p) for p in split_top(args)]
    return Func(m.group(2), ret, params)


class Headers:
    def __init__(self):
        self.structs = {}           # name -> Struct
        self.typedefs = {}          # name -> base scalar name (ShardEntity -> uint64_t)
        self.callbacks = {}         # Fn typedef name -> Func
        self.defines = {}           # SHARD_IFACE_X -> "shard.x"
        self.interface_minor = {}   # header -> minor
        self.order = []             # struct names in file order

    def read(self, header):
        raw = open(os.path.join(C_DIR, header), encoding='utf-8').read().replace('\r\n', '\n')
        m = re.search(r'Interface "([\w.]+)" (\d+)\.(\d+)', raw)
        if m:
            self.interface_minor[header] = (m.group(1), int(m.group(2)), int(m.group(3)))
        for d in re.finditer(r'#define\s+(SHARD_IFACE_\w+)\s+"([\w.]+)"', raw):
            self.defines[d.group(1)] = d.group(2)
        text = strip_comments(raw)
        # typedef struct Name { ... } Name;
        for sm in re.finditer(r'typedef\s+struct\s+(\w+)\s*\{(.*?)\}\s*(\w+)\s*;', text):
            name = sm.group(3)
            s = Struct(name)
            for decl in sm.group(2).split(';'):
                decl = decl.strip()
                if not decl:
                    continue
                fn = parse_func(decl)
                if fn:
                    s.funcs.append(fn)
                    s.fields.append((fn.name, CType('<fnptr>', 1)))
                    continue
                # "uint32_t abiMajor, abiMinor" / "float min, max" / "const char* data"
                first = parse_declarator(decl.split(',')[0])
                s.fields.append((first.name, first.type))
                for extra in decl.split(',')[1:]:
                    nm = extra.strip()
                    am = re.match(r'^(\w+)\s*\[\s*(\w+)\s*\]$', nm)
                    t = CType(first.type.base, first.type.ptr, first.type.const, am.group(2) if am else None)
                    s.fields.append((am.group(1) if am else nm, t))
            self.structs[name] = s
            self.order.append(name)
        # typedef <scalar> Name ;  and  typedef <ret> (SHARD_CALL *Name)(...) ;
        for tm in re.finditer(r'typedef\s+([^;{}]*?)\s*;', text):
            body = tm.group(1)
            if body.startswith('struct '):
                continue
            fn = parse_func(body)
            if fn:
                self.callbacks[fn.name] = fn
                continue
            parts = body.split()
            if len(parts) == 2 and parts[0] in SCALARS:
                self.typedefs[parts[1]] = parts[0]


def define_integers(raw_headers):
    """Not needed for the layouts : array sizes in the headers are written as numbers"""
    return {}


# ---------------------------------------------------------------------------------------------------- layouts (wasm32)

class Layout:
    def __init__(self, size, align, offsets):
        self.size, self.align, self.offsets = size, align, offsets


def scalar_of(h, base):
    """The scalar a name stands for, or None"""
    if base in SCALARS:
        return base
    return h.typedefs.get(base)


def type_layout(h, t, cache):
    """(size, alignment) of a field type on wasm32"""
    if t.ptr:
        size, align = 4, 4
    elif scalar_of(h, t.base):
        size, align, _ = SCALARS[scalar_of(h, t.base)]
    elif t.base in h.structs:
        lay = struct_layout(h, t.base, cache)
        size, align = lay.size, lay.align
    elif t.base in h.callbacks:
        size, align = 4, 4
    else:
        raise GenError('the type "%s" has no wasm32 layout here' % t.base)
    if t.array:
        size *= int(t.array)
    return size, align


def struct_layout(h, name, cache):
    if name in cache:
        return cache[name]
    s = h.structs[name]
    offset, maxalign, offsets = 0, 1, {}
    for fname, ftype in s.fields:
        size, align = type_layout(h, ftype, cache)
        offset = (offset + align - 1) // align * align
        offsets[fname] = (offset, size)
        offset += size
        maxalign = max(maxalign, align)
    size = (offset + maxalign - 1) // maxalign * maxalign
    cache[name] = Layout(size, maxalign, offsets)
    return cache[name]


def is_plain(h, name, cache, seen=None):
    """A struct with no pointer and no ShardStr : the same bytes on the host and in the module"""
    s = h.structs.get(name)
    if not s:
        return False
    for fname, ftype in s.fields:
        if ftype.ptr or ftype.base == '<fnptr>':
            return False
        if ftype.base in h.structs and not is_plain(h, ftype.base, cache):
            return False
        if ftype.base not in h.structs and not scalar_of(h, ftype.base):
            return False
    return True


# ---------------------------------------------------------------------------------------------------- classification

HANDLE_NAMES = None


def wasm_letter(h, t):
    """The wasm32 value type a parameter of this C type has (Clang's ABI : ShardStr by value goes by address)"""
    if t.ptr or t.array:
        return 'I'
    if t.base == 'ShardStr':
        return 'I'
    if t.base in h.callbacks:
        return 'I'
    sc = scalar_of(h, t.base)
    if sc:
        return WASM_LETTER[SCALARS[sc][2]]
    raise GenError('no wasm32 value type for "%s"' % t)


def wasm_signature(h, fn):
    params = []
    if fn.ret.base == 'ShardStr' and not fn.ret.ptr:
        params.append('I')                  # the result is returned through a pointer the caller passes first
        results = []
    elif fn.ret.base == 'void' and not fn.ret.ptr:
        results = []
    else:
        results = [wasm_letter(h, fn.ret)]
    for p in fn.params:
        params.append(wasm_letter(h, p.type))
    return params, results


class Unclassified(Exception):
    pass


def c_type_name(t):
    return ('const ' if t.const else '') + t.base + '*' * t.ptr


class Emit:
    """The host-side body of one generated import"""

    def __init__(self):
        self.pre = []
        self.args = []
        self.post = []
        self.wrap = None            # ('text'|'array', slot, element type, args list index range)
        self.slot = 0


def classify(h, cache, api, fn):
    """Builds the host-side code of `fn`, or raises Unclassified"""
    e = Emit()
    params = fn.params
    ret_str = fn.ret.base == 'ShardStr' and not fn.ret.ptr
    e.slot = 1 if ret_str else 0
    i = 0
    n = len(params)
    while i < n:
        p = params[i]
        t = p.type
        name = 'p%d' % i
        slot = e.slot
        # the plugin's context : the sandbox's own, whatever it passes
        if t.base == 'ShardPluginCtx' and not t.ptr and i == 0:
            e.args.append('s.ctx')
            e.slot += 1
            i += 1
            continue
        # text out : char* buffer, uint64_t capacity, uint64_t* size
        if (t.base == 'char' and t.ptr == 1 and not t.const and i + 2 < n + 0 and
                params[i + 1].type.base == 'uint64_t' and not params[i + 1].type.ptr and
                params[i + 2].type.base == 'uint64_t' and params[i + 2].type.ptr == 1 and i + 3 == n):
            e.wrap = ('text', slot, None, len(e.args))
            e.args += ['tb', 'tc', 'ts']
            e.slot += 3
            i += 3
            continue
        # array out : T* out, uint32_t capacity, uint32_t* count
        if (t.ptr == 1 and not t.const and not t.array and i + 2 < n + 0 and
                params[i + 1].type.base == 'uint32_t' and not params[i + 1].type.ptr and
                params[i + 2].type.base == 'uint32_t' and params[i + 2].type.ptr == 1 and i + 3 == n and
                scalar_of(h, t.base) and SCALARS[scalar_of(h, t.base)][0] == 8):
            e.wrap = ('array', slot, t.base, len(e.args))
            e.args += ['tb', 'tc', 'ts']
            e.slot += 3
            i += 3
            continue
        # a text buffer edited in place : char* buffer, uint64_t capacity
        if t.base == 'char' and t.ptr == 1 and not t.const and i + 1 < n and params[i + 1].type.base == 'uint64_t' and not params[i + 1].type.ptr:
            e.pre.append('InOutText %s(in, a[%d], a[%d]); if (!%s.ok) return false;' % (name, slot, slot + 1, name))
            e.args += ['%s.Data()' % name, '%s.Capacity()' % name]
            e.post.append('if (!%s.Commit()) return false;' % name)
            e.slot += 2
            i += 2
            continue
        # floats and their count : const float* values, int32_t count
        if t.base == 'float' and t.ptr == 1 and t.const and i + 1 < n and params[i + 1].type.base == 'int32_t' and not params[i + 1].type.ptr:
            e.pre.append('std::vector<float> %s; if (!ReadFloats(in, a[%d], a[%d], %s)) return false;' % (name, slot, slot + 1, name))
            e.args += ['%s.empty() ? nullptr : %s.data()' % (name, name), 'int32_t(%s.size())' % name]
            e.slot += 2
            i += 2
            continue
        # bytes in : const void* data, uint64_t size
        if t.base == 'void' and t.ptr == 1 and t.const and i + 1 < n and params[i + 1].type.base == 'uint64_t' and not params[i + 1].type.ptr:
            e.pre.append('std::vector<uint8_t> %s; if (!ReadBytes(in, a[%d], a[%d], %s)) return false;' % (name, slot, slot + 1, name))
            e.args += ['%s.empty() ? nullptr : %s.data()' % (name, name), 'uint64_t(a[%d])' % (slot + 1)]
            e.slot += 2
            i += 2
            continue
        # a string by value
        if t.base == 'ShardStr' and not t.ptr:
            e.pre.append('std::string %s; if (!ReadStr(in, a[%d], %s)) return false;' % (name, slot, name))
            e.args.append('ToStr(%s)' % name)
            e.slot += 1
            i += 1
            continue
        # three floats, in or out
        if t.array in ('2', '3', '4') and t.base == 'float':
            e.pre.append('FloatArray<%s> %s(in, a[%d]); if (!%s.ok) return false;' % (t.array, name, slot, name))
            e.args.append('%s.v' % name)
            if not t.const:
                e.post.append('if (!%s.Commit()) return false;' % name)
            e.slot += 1
            i += 1
            continue
        # a pointer to a number, a handle or a plain struct
        if t.ptr == 1 and not t.array and (scalar_of(h, t.base) or (t.base in h.structs and is_plain(h, t.base, cache))):
            native = t.base
            if t.const:
                e.pre.append('InArg<%s> %s(in, a[%d]); if (!%s.ok) return false;' % (native, name, slot, name))
                e.args.append('&%s.value' % name)
            else:
                e.pre.append('OutArg<%s> %s(in, a[%d]); if (!%s.ok) return false;' % (native, name, slot, name))
                e.args.append('&%s.value' % name)
                e.post.append('if (!%s.Commit()) return false;' % name)
            e.slot += 1
            i += 1
            continue
        # a number or a handle by value
        if not t.ptr and not t.array and scalar_of(h, t.base):
            sc = scalar_of(h, t.base)
            if sc == 'float':
                e.args.append('F32(a[%d])' % slot)
            elif sc == 'double':
                e.args.append('F64(a[%d])' % slot)
            elif SCALARS[sc][0] == 8:
                e.args.append('static_cast<%s>(a[%d])' % (t.base, slot))
            elif sc.startswith('int'):
                e.args.append('static_cast<%s>(int32_t(uint32_t(a[%d])))' % (t.base, slot))
            else:
                e.args.append('static_cast<%s>(uint32_t(a[%d]))' % (t.base, slot))
            e.slot += 1
            i += 1
            continue
        raise Unclassified('%s.%s : parameter "%s %s" (%s) has no generic marshalling : write it by hand and add it to MANUAL' % (api, fn.name, c_type_name(t), p.name, t))
    return e


def result_kind(h, fn):
    t = fn.ret
    if t.ptr:
        raise Unclassified('%s returns a pointer' % fn.name)
    if t.base == 'void':
        return 'void'
    if t.base == 'ShardStr':
        return 'str'
    sc = scalar_of(h, t.base)
    if sc == 'float':
        return 'float'
    if sc == 'double':
        return 'double'
    if sc in ('int32_t', 'uint32_t'):
        return 'ShardResult' if t.base == 'ShardResult' else 'int'
    if sc in ('int64_t', 'uint64_t'):
        return 'long'
    raise Unclassified('%s returns %s' % (fn.name, t.base))


# ---------------------------------------------------------------------------------------------------- output

BANNER = ('GENERATED by scripts/gen_wasm_imports.py from the headers of sdk/include/shard : do not edit, change a header or the script and run\n'
          'python scripts/gen_wasm_imports.py (the test Wasm.GeneratedImports checks it is up to date).')


def cname(*parts):
    return '_'.join(parts)


def gen_sdk_header(h, interfaces):
    out = []
    w = out.append
    w('/* ' + BANNER.replace('\n', '\n * ') + '\n *')
    w(' * The module side of a sandboxed plugin (tier 2, "abi": "wasm1") : every function of the interfaces the sandbox offers is an import of the')
    w(' * host (module "shard"), named "<interface>.<function>", with the signature Clang gives the C function on wasm32 (a ShardStr by value, and')
    w(' * every struct, goes by address). The tables of the C ABI (ShardCVarsApi...) are built from those imports, and the ShardHostApi the plugin')
    w(' * is handed is shard_wasm_host_table : the host reads its address from the exported global of that name (wasm-ld --export=shard_wasm_host_table)')
    w(' * and passes it to shard_plugin_entry, so a plugin written for the C ABI compiles unchanged for wasm32. See shard_wasm.h. */')
    w('#ifndef SHARD_WASM_IMPORTS_H')
    w('#define SHARD_WASM_IMPORTS_H')
    w('')
    w('#include "shard_abi.h"')
    w('#include "shard_host.h"')
    for header, struct, short, perm in interfaces:
        w('#include "%s"' % header)
    w('')
    w('#if defined(__wasm__)')
    w('')
    w('#ifdef __cplusplus')
    w('extern "C" {')
    w('#endif')
    w('')
    w('#define SHARD_WASM_IMPORT(name) __attribute__((import_module("shard"), import_name(name)))')
    w('')
    w('/* ---- The host, as ShardHostApi ---- */')
    w('SHARD_WASM_IMPORT("log") void shard_wasm_host_log(ShardPluginCtx ctx, uint32_t level, ShardStr message);')
    w('SHARD_WASM_IMPORT("alloc") void* shard_wasm_host_alloc(ShardPluginCtx ctx, uint64_t size, uint64_t align);')
    w('SHARD_WASM_IMPORT("free") void shard_wasm_host_free(ShardPluginCtx ctx, void* p);')
    w('/* SHARD_OK if the host offers that interface to this plugin (its permissions allow it, the revision is there), SHARD_E_NOT_FOUND otherwise */')
    w('SHARD_WASM_IMPORT("query_interface") ShardResult shard_wasm_interface_offered(ShardStr id, uint32_t major, uint32_t minMinor);')
    w('')
    for header, struct, short, perm in list(interfaces) + TABLES:
        api = h.structs[struct]
        w('/* ---- shard.%s ---- */' % short)
        for fn in api.funcs:
            decl = fn_decl(h, fn, 'shard_wasm_%s_%s' % (short, fn.name))
            w('SHARD_WASM_IMPORT("%s.%s") %s;' % (short, fn.name, decl))
        w('')
    w('#ifdef __cplusplus')
    w('}')
    w('#endif')
    w('')
    for header, struct, short, perm in interfaces:
        api = h.structs[struct]
        w('static const %s shard_wasm_api_%s = {' % (struct, short))
        w('    sizeof(%s),' % struct)
        for fn in api.funcs:
            w('    shard_wasm_%s_%s,' % (short, fn.name))
        w('};')
    w('')
    w('typedef struct ShardWasmInterface {')
    w('    const char* id;')
    w('    uint32_t    idSize;')
    w('    const void* table;')
    w('} ShardWasmInterface;')
    w('')
    w('static const ShardWasmInterface shard_wasm_interfaces[] = {')
    for header, struct, short, perm in interfaces:
        iface = h.interface_minor[header][0]
        w('    { "%s", %d, &shard_wasm_api_%s },' % (iface, len(iface), short))
    w('};')
    w('')
    w('/* Looks an interface up like ShardHostApi::query_interface does : the host says whether it offers it to this plugin, the table is the module\'s */')
    w('static ShardResult shard_wasm_query_interface(ShardStr id, uint32_t major, uint32_t minMinor, const void** out)')
    w('{')
    w('    uint32_t i;')
    w('    if (!out) return SHARD_E_INVALID_ARG;')
    w('    *out = 0;')
    w('    for (i = 0; i < sizeof(shard_wasm_interfaces) / sizeof(shard_wasm_interfaces[0]); ++i) {')
    w('        const ShardWasmInterface* entry = &shard_wasm_interfaces[i];')
    w('        uint64_t k = 0;')
    w('        if (entry->idSize != id.size) continue;')
    w('        while (k < id.size && entry->id[k] == id.data[k]) ++k;')
    w('        if (k != id.size) continue;')
    w('        {')
    w('            const ShardResult offered = shard_wasm_interface_offered(id, major, minMinor);')
    w('            if (offered == SHARD_OK) *out = entry->table;')
    w('            return offered;')
    w('        }')
    w('    }')
    w('    return SHARD_E_NOT_FOUND;')
    w('}')
    w('')
    w('/* What shard_plugin_entry receives as `host`. The module exports it (--export=shard_wasm_host_table) ; every file that includes this header')
    w(' * defines it, the linker keeps one. */')
    w('#ifdef __cplusplus')
    w('extern "C" {')
    w('#endif')
    w('extern const ShardHostApi shard_wasm_host_table;')
    w('__attribute__((weak, used)) const ShardHostApi shard_wasm_host_table = {')
    w('    sizeof(ShardHostApi), SHARD_ABI_MAJOR, SHARD_ABI_MINOR, { 0, 0 },')
    w('    shard_wasm_host_log, shard_wasm_host_alloc, shard_wasm_host_free, shard_wasm_query_interface,')
    w('};')
    for header, struct, short, perm in TABLES:
        w('')
        w('/* What a callback of the plugin is handed (%s) : the module passes its own, built from the imports, and exports it */' % struct)
        w('extern const %s shard_wasm_%s_table;' % (struct, short))
        w('__attribute__((weak, used)) const %s shard_wasm_%s_table = {' % (struct, short))
        w('    sizeof(%s),' % struct)
        for fn in h.structs[struct].funcs:
            w('    shard_wasm_%s_%s,' % (short, fn.name))
        w('};')
    w('#ifdef __cplusplus')
    w('}')
    w('#endif')
    w('')
    w('')
    w("/* The wasm32 layouts the host reads and writes (src/engine/plugins/wasm/wasm_layout_gen.hpp) against Clang's : define SHARD_WASM_CHECK_LAYOUT to compile the check */")
    w('#ifdef SHARD_WASM_CHECK_LAYOUT')
    w('#include <stddef.h>')
    w('#ifdef __cplusplus')
    w('#   define SHARD_WASM_ASSERT(cond, what) static_assert(cond, what)')
    w('#   define _Alignof alignof')
    w('#else')
    w('#   define SHARD_WASM_ASSERT(cond, what) _Static_assert(cond, what)')
    w('#endif')
    for line in gen_layout_checks(h, interfaces):
        w(line)
    w('#endif')
    w('')
    w('#endif /* __wasm__ */')
    w('')
    w('#endif /* SHARD_WASM_IMPORTS_H */')
    return '\n'.join(out) + '\n'


def fn_decl(h, fn, cname_):
    params = ', '.join(param_decl(p) for p in fn.params) or 'void'
    return '%s %s(%s)' % (c_type_name(fn.ret), cname_, params)


def param_decl(p):
    t = p.type
    name = c_type_name(t) + ' ' + p.name
    if t.array:
        name += '[%s]' % t.array
    return name


def gen_layout_header(h, interfaces):
    cache = {}
    names = []
    for header in [i[0] for i in interfaces] + EXTRA_HEADERS:
        for n in h.order:
            if n in names:
                continue
        # order : the order of the files
    names = list(h.order)
    out = []
    w = out.append
    w('#pragma once')
    w('')
    w('// ' + BANNER.replace('\n', '\n// '))
    w('//')
    w('// The wasm32 layout of every struct of the C ABI (pointers and function pointers are 4 bytes, a 64-bit number is aligned on 8) : what the host')
    w('// reads from and writes to the memory of a sandboxed plugin, field by field. Clang\'s own layout is checked against it when the test plugin is')
    w('// compiled (SHARD_WASM_CHECK_LAYOUT in shard_wasm_imports.h).')
    w('')
    w('#include <cstdint>')
    w('')
    for header, struct, short, perm in interfaces:
        w('#include <shard/%s>' % header)
    w('')
    w('namespace Shard::Engine::Plugins::Wasm {')
    w('')
    w('    /// A permission of the manifest (`permissions`) that gates an interface or a function of the sandbox')
    perms = []
    for header, struct, short, perm in interfaces:
        if perm and perm not in perms:
            perms.append(perm)
    for k, v in FUNCTION_PERMISSIONS.items():
        if v not in perms:
            perms.append(v)
    enum_names = [perm_enum(p) for p in perms]
    w('    enum class Permission : uint8_t { None, ' + ', '.join(enum_names) + ' };')
    w('    inline constexpr const char* kPermissionNames[] = { "", ' + ', '.join('"%s"' % p for p in perms) + ' };')
    w('    inline constexpr uint32_t kPermissionCount = %d;' % (len(perms) + 1))
    w('')
    w('    /// The interfaces the sandbox offers : the id, the revision (minor) of the header, and the permission without which it is not offered')
    w('    struct OfferedInterface { const char* id; const char* shortName; uint32_t minor; Permission permission; };')
    w('    inline constexpr OfferedInterface kOfferedInterfaces[] = {')
    for header, struct, short, perm in interfaces:
        iface, major, minor = h.interface_minor[header]
        w('        { "%s", "%s", %d, Permission::%s },' % (iface, short, minor, perm_enum(perm) if perm else 'None'))
    w('    };')
    w('')
    w("    /// The native tables of the host, as the sandbox's imports call them (null where the host does not have the interface)")
    w('    struct HostApis {')
    for header, struct, short, perm in interfaces:
        w('        const %s* %s = nullptr;' % (struct, short))
    w('    };')
    w('    template <typename Query>')
    w('    HostApis LoadHostApis(Query&& query)')
    w('    {')
    w('        HostApis apis;')
    for header, struct, short, perm in interfaces:
        iface, major, minor = h.interface_minor[header]
        w('        apis.%s = static_cast<const %s*>(query("%s", %d));' % (short, struct, iface, minor))
    w('        return apis;')
    w('    }')
    w('')
    w('    namespace Layout {')
    for n in names:
        s = h.structs[n]
        lay = struct_layout(h, n, cache)
        w('        namespace %s {' % n)
        w('            inline constexpr uint32_t kSize = %d, kAlign = %d;' % (lay.size, lay.align))
        for fname, ftype in s.fields:
            off, size = lay.offsets[fname]
            w('            inline constexpr uint32_t o_%s = %d;' % (fname, off))
        w('        }')
    w('    }')
    w('}')
    return '\n'.join(out) + '\n'


def perm_enum(p):
    return ''.join(x.capitalize() for x in re.split(r'[.:_]', p))


def gen_layout_checks(h, interfaces):
    """The assertions of shard_wasm_imports.h's layout check : Clang's wasm32 layout of every struct against the one computed here"""
    cache = {}
    out = []
    for n in h.order:
        s = h.structs[n]
        lay = struct_layout(h, n, cache)
        out.append('SHARD_WASM_ASSERT(sizeof(%s) == %d && _Alignof(%s) == %d, "%s");' % (n, lay.size, n, lay.align, n))
        for fname, ftype in s.fields:
            out.append('SHARD_WASM_ASSERT(offsetof(%s, %s) == %d, "%s.%s");' % (n, fname, lay.offsets[fname][0], n, fname))
    return out


def host_value_letter(letter):
    return {'I': 'I', 'L': 'L', 'F': 'F', 'D': 'D'}[letter]


def gen_host(h, interfaces):
    cache = {}
    out = []
    w = out.append
    w('// ' + BANNER.replace('\n', '\n// '))
    w('//')
    w('// The host side of the imports whose arguments are numbers, strings, buffers and plain structs (the others are in wasm_imports_*.cpp).')
    w('')
    w('#include "engine/plugins/wasm/wasm_imports.hpp"')
    w('')
    w('#include <cstring>')
    w('')
    w('#include "engine/plugins/wasm/wasm_marshal.hpp"')
    w('#include "engine/plugins/wasm/wasm_sandbox.hpp"')
    w('')
    w('namespace Shard::Engine::Plugins::Wasm {')
    w('')
    w('    namespace {')
    w('        constexpr ValType I = ValType::I32, L = ValType::I64, F = ValType::F32, D = ValType::F64;')
    w('        using namespace Marshal;')
    w('    }')
    w('')
    w('    const std::vector<ImportSignature>& AllImportSignatures()')
    w('    {')
    w('        static const std::vector<ImportSignature> all = {')
    for header, struct, short, perm in list(interfaces) + TABLES:
        for fn in h.structs[struct].funcs:
            params, results = wasm_signature(h, fn)
            full = '%s.%s' % (short, fn.name)
            manual = full in MANUAL
            w('            { "%s", { { %s }, { %s } }, %s },' % (full, ', '.join(WASM_LETTER_CPP[c] for c in params), ', '.join(WASM_LETTER_CPP[c] for c in results), 'true' if manual else 'false'))
    w('            { "log", { { L, I, I }, {} }, true },')
    w('            { "alloc", { { L, L, L }, { I } }, true },')
    w('            { "free", { { L, I }, {} }, true },')
    w('            { "query_interface", { { I, I, I }, { I } }, true },')
    w('        };')
    w('        return all;')
    w('    }')
    w('')
    w('    void AddGeneratedImports(Sandbox& s, ImportList& imports, const HostApis& apis)')
    w('    {')
    for header, struct, short, perm in interfaces:
        api = h.structs[struct]
        generated = [fn for fn in api.funcs if '%s.%s' % (short, fn.name) not in MANUAL]
        if not generated:
            continue
        w('        // ---- shard.%s ----' % short)
        w('        {')
        w('            const %s* api = apis.%s;' % (struct, short))
        for fn in generated:
            full = '%s.%s' % (short, fn.name)
            emit_host_function(h, cache, w, interfaces, short, struct, perm, fn, full)
        w('        }')
        w('')
    for header, struct, short, perm in TABLES:
        api = h.structs[struct]
        generated = [fn for fn in api.funcs if '%s.%s' % (short, fn.name) not in MANUAL]
        w('        // ---- the %s the host is drawing with ----' % short)
        w('        {')
        for fn in generated:
            emit_host_function(h, cache, w, interfaces, short, struct, perm, fn, '%s.%s' % (short, fn.name), dynamic=short)
        w('        }')
        w('')
    w('    }')
    w('}')
    return '\n'.join(out) + '\n'


WASM_LETTER_CPP = {'I': 'I', 'L': 'L', 'F': 'F', 'D': 'D'}


def denied_statement(kind):
    if kind == 'ShardResult':
        return 'r[0] = uint32_t(SHARD_E_UNSUPPORTED);'
    if kind in ('int', 'long', 'float', 'double'):
        return 'r[0] = 0;'
    return ''


def emit_host_function(h, cache, w, interfaces, short, struct, iface_perm, fn, full, dynamic=None):
    kind = result_kind(h, fn)
    params, results = wasm_signature(h, fn)
    sig = '{ { %s }, { %s } }' % (', '.join(params), ', '.join(results))
    perm = FUNCTION_PERMISSIONS.get(full) or iface_perm
    guard_perm = perm_enum(perm) if perm else None
    denied = full in DENIED_ALWAYS
    w('            imports.Add("%s", %s, [&s%s](Instance& in, const uint64_t* a, uint64_t* r) -> bool {' % (full, sig, '' if dynamic else ', api'))
    w('                (void)in; (void)a; (void)r;')
    if dynamic:
        w('                const %s* api = s.%s;' % (struct, dynamic))
    cond = ['!api', '!SHARD_HAS(api, %s)' % fn.name]
    if denied:
        cond = ['true']
    if guard_perm:
        cond.append('!s.Allowed(Permission::%s)' % guard_perm)
    w('                if (%s) { %s return true; }' % (' || '.join(cond), denied_statement(kind)))
    if denied:
        w('            });')
        return
    e = classify(h, cache, struct, fn)
    for line in e.pre:
        w('                ' + line)
    # the call
    args = list(e.args)
    call_args = ', '.join(args)
    if e.wrap:
        wkind, slot, elem, at = e.wrap
        # the wrapped triple is tb, tc, ts in the argument list
        if wkind == 'text':
            w('                ShardResult rv = SHARD_OK;')
            w('                if (!TextOut(in, a[%d], a[%d], a[%d], rv, [&](char* tb, uint64_t tc, uint64_t* ts) { return api->%s(%s); })) return false;' % (slot, slot + 1, slot + 2, fn.name, call_args))
        else:
            w('                ShardResult rv = SHARD_OK;')
            w('                if (!ArrayOut<%s>(in, a[%d], a[%d], a[%d], rv, [&](%s* tb, uint32_t tc, uint32_t* ts) { return api->%s(%s); })) return false;' % (elem, slot, slot + 1, slot + 2, elem, fn.name, call_args))
        for line in e.post:
            w('                ' + line)
        w('                r[0] = uint32_t(rv);')
        w('                return true;')
        w('            });')
        return
    call = 'api->%s(%s)' % (fn.name, call_args)
    if kind == 'void':
        w('                %s;' % call)
        for line in e.post:
            w('                ' + line)
        w('                return true;')
    elif kind == 'str':
        w('                const ShardStr rv = %s;' % call)
        w('                return s.ReturnString(in, a[0], rv);')
    else:
        w('                const auto rv = %s;' % call)
        for line in e.post:
            w('                ' + line)
        if kind == 'float':
            w('                r[0] = Bits(rv);')
        elif kind == 'double':
            w('                r[0] = Bits(rv);')
        elif kind == 'long':
            w('                r[0] = uint64_t(rv);')
        else:
            w('                r[0] = uint32_t(rv);')
        w('                return true;')
    w('            });')


def generate():
    h = Headers()
    for header, struct, short, perm in INTERFACES:
        h.read(header)
    for header in EXTRA_HEADERS:
        h.read(header)
    # the interface headers also include types of others (ShardEntity...) : read in the order above, typedefs are global
    for header, struct, short, perm in INTERFACES:
        if struct not in h.structs:
            raise GenError('%s has no table %s' % (header, struct))
        if header not in h.interface_minor:
            raise GenError('%s announces no "Interface" revision' % header)
    # every function either has a generic marshalling or is listed as written by hand
    cache = {}
    seen_manual = set()
    for header, struct, short, perm in INTERFACES + TABLES:
        for fn in h.structs[struct].funcs:
            full = '%s.%s' % (short, fn.name)
            wasm_signature(h, fn)
            if full in MANUAL:
                seen_manual.add(full)
                continue
            if full in DENIED_ALWAYS:
                continue
            classify(h, cache, struct, fn)
            result_kind(h, fn)
    stale = sorted(set(MANUAL) - seen_manual)
    if stale:
        raise GenError('MANUAL names functions the headers no longer have : ' + ', '.join(stale))
    return {
        OUT_SDK: gen_sdk_header(h, INTERFACES),
        OUT_LAYOUT: gen_layout_header(h, INTERFACES),
        OUT_HOST: gen_host(h, INTERFACES),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--check', action='store_true', help='fail if a generated file is not up to date')
    args = parser.parse_args()
    try:
        files = generate()
    except (GenError, Unclassified) as e:
        print('gen_wasm_imports : ' + str(e), file=sys.stderr)
        return 2
    status = 0
    for path, text in files.items():
        if args.check:
            current = open(path, encoding='utf-8').read().replace('\r\n', '\n') if os.path.exists(path) else ''
            if current != text:
                print(path + ' is not what the headers give : run python scripts/gen_wasm_imports.py', file=sys.stderr)
                status = 1
            continue
        with open(path, 'w', encoding='utf-8', newline='\n') as f:
            f.write(text)
        print('wrote ' + path)
    if args.check and status == 0:
        print('the generated wasm imports are up to date')
    return status


if __name__ == '__main__':
    sys.exit(main())
