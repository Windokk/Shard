#!/usr/bin/env python3
"""Writes docs/docs/api_reference.md from the headers of the SDK : the comments of sdk/include/shard/*.h (the C ABI) and the /// comments of
sdk/include/shard/cpp/*.hpp (the C++ SDK). The headers are the reference : this page is only their reading order, so never edit it by hand.

    python scripts/gen_api_reference.py            writes the page
    python scripts/gen_api_reference.py --check    fails (exit 1) if the page is not what the headers give : the test Docs.ApiReference

It also checks that the revision each header announces ("Interface "shard.x" 1.1 : ...") is the one the engine offers in query_interface
(src/engine/plugins/abi/c_plugin.cpp) : a table that grew without its revision following is an error here.
"""

import argparse
import os
import re
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
C_DIR = os.path.join(ROOT, 'sdk', 'include', 'shard')
CPP_DIR = os.path.join(C_DIR, 'cpp')
HOST_TABLE = os.path.join(ROOT, 'src', 'engine', 'plugins', 'abi', 'c_plugin.cpp')
OUT = os.path.join(ROOT, 'docs', 'docs', 'api_reference.md')

# The reading order ; a header not listed comes after these, by name
C_ORDER = ['shard_abi.h', 'shard_host.h', 'shard_plugin.h', 'shard_cvars.h', 'shard_commands.h', 'shard_components.h', 'shard_ecs.h',
           'shard_systems.h', 'shard_events.h', 'shard_hooks.h', 'shard_services.h', 'shard_extensions.h', 'shard_content.h', 'shard_state.h']
C_SKIP = ['shard.h']        # includes the others, nothing of its own
CPP_ORDER = ['plugin.hpp', 'component.hpp', 'system.hpp', 'cvars.hpp', 'events.hpp', 'services.hpp', 'content.hpp', 'string.hpp']
CPP_SKIP = ['detail.hpp']   # what the others share : shard::Error is the only thing a plugin sees of it, documented in the page's text

BS = chr(92)


class GenError(Exception):
    pass


def ordered(folder, order, skip, ext):
    files = sorted(f for f in os.listdir(folder) if f.endswith(ext) and f not in skip)
    return [f for f in order if f in files] + [f for f in files if f not in order]


# ---------------------------------------------------------------------------------------------------- prose

IDENT = re.compile(r'("[^"\n]*"|\bshard\.[a-z][a-z_.]*[a-z]\b|mod://[^\s,;)]*|(?<![\w)\]])\*\w+\b|\b[a-z]+_\*|\b[\w.-]+(?:/[\w.-]+)*\.(?:hpp|h|cpp|c|md|json|cmake)\b|\b\w+(?:/[\w.<>-]+)+/?|\b\w+::[\w:]+(?:<\w+>)?|\bSHARD_[A-Z0-9_]+\b\*?|\bShard[A-Z][A-Za-z0-9]*\b|\b[a-z][a-z0-9]*(?:_[a-z0-9]+)+\b(?:\(\))?|\b\w+\(\))')


def inline(text):
    """Identifiers, paths and quoted names in code spans ; what Markdown would read as markup escaped elsewhere. What the header already put
    between backticks stays as it is."""
    out = []
    for k, part in enumerate(text.split('`')):
        if k % 2 == 1:
            out.append('`' + part + '`')
            continue
        pos = 0
        for m in IDENT.finditer(part):
            out.append(escape(part[pos:m.start()]))
            out.append('`' + m.group(0) + '`')
            pos = m.end()
        out.append(escape(part[pos:]))
    return ''.join(out)


def escape(text):
    text = text.replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;')
    text = text.replace('*', BS + '*').replace('_', BS + '_').replace('|', BS + '|')
    return text


def prose(lines):
    """Comment lines (markers removed) as Markdown : paragraphs, lists, and aligned blocks (tables, examples) kept as they are."""
    out = []
    para = []
    block = []

    def flush_para():
        if para:
            out.append(inline(' '.join(para)))
            out.append('')
            para.clear()

    def flush_block():
        if block:
            # the common indentation goes
            indent = min(len(b) - len(b.lstrip()) for b in block if b.strip())
            out.append('```')
            out.extend(b[indent:].rstrip() for b in block)
            out.append('```')
            out.append('')
            block.clear()

    for line in lines:
        stripped = line.strip()
        if not stripped:
            flush_para()
            flush_block()
            continue
        heading = re.match(r'^-{2,}\s*(.*?)\s*-{2,}$', stripped)
        if heading:
            flush_para()
            flush_block()
            out.append('**' + inline(heading.group(1)) + '**')
            out.append('')
            continue
        aligned = line.startswith('  ') and not re.match(r'^\s*[-*] ', line)
        aligned = aligned or re.search(r'\S {3,}\S', stripped) is not None
        if aligned:
            flush_para()
            block.append(line)
            continue
        flush_block()
        item = re.match(r'^\s*[-*] (.*)$', line)
        if item:
            flush_para()
            out.append('* ' + inline(item.group(1)))
            continue
        if out and out[-1].startswith('* ') and not para and line[:1].isspace():
            # a list item that goes on on the next line
            out[-1] += ' ' + inline(stripped)
            continue
        if out and out[-1].startswith('* ') and not para:
            out.append('')      # a paragraph after a list : not a lazy continuation of its last item
        para.append(stripped)
    flush_para()
    flush_block()
    while out and out[-1] == '':
        out.pop()
    return out


def comment_lines(comment):
    """The lines of a /* */ comment, without the markers"""
    body = comment[2:-2]
    lines = body.split('\n')
    result = []
    for l in lines:
        l = re.sub(r'^\s*\* ?', '', l) if re.match(r'^\s*\*(\s|$)', l) else l
        result.append(l.rstrip())
    # the first line follows "/*" : its leading space goes
    if result:
        result[0] = result[0].lstrip()
    while result and not result[-1].strip():
        result.pop()
    while result and not result[0].strip():
        result.pop(0)
    # continuation lines are indented under the first one : that indentation is not alignment
    if len(result) > 1:
        indents = [len(l) - len(l.lstrip()) for l in result[1:] if l.strip()]
        common = min(indents) if indents else 0
        result = [result[0]] + [l[common:] if len(l) >= common else l.lstrip() for l in result[1:]]
    return result


# ---------------------------------------------------------------------------------------------------- C headers

def clean_code(code):
    code = ' '.join(code.split())
    code = code.replace('SHARD_CALL ', '')
    code = re.sub(r'\(\s*\*\s*(\w+)\s*\)\s*\(', r'\1(', code) if not code.startswith('typedef') else code.replace('( *', '(*').replace('(* ', '(*')
    code = re.sub(r'\(\s+', '(', code)
    code = re.sub(r'\s+\)', ')', code)
    return code


def parse_c(text, name):
    """Items in file order : ('comment', text, line), ('define', name, value, trailing), ('code', code, trailing), ('struct', name, items)"""
    # what is not part of the reference : guards, includes, extern "C", platform switches
    keep = []
    for line in text.split('\n'):
        s = line.strip()
        if re.match(r'^#\s*(ifndef|ifdef|if|else|elif|endif|include|pragma)\b', s):
            continue
        if s in ('extern "C" {', '}') or re.match(r'^#\s*define\s+SHARD_\w+_H\s*$', s):
            continue
        keep.append(line)
    text = '\n'.join(keep)
    return scan(text, 0, len(text), name)


def scan(text, pos, end, where):
    items = []
    line_of_last_code = -1

    def line_at(p):
        return text.count('\n', 0, p)

    while pos < end:
        if text[pos].isspace():
            pos += 1
            continue
        if text.startswith('/*', pos):
            close = text.find('*/', pos)
            if close < 0:
                raise GenError(where + ' : a comment does not end')
            comment = text[pos:close + 2]
            if items and items[-1][0] in ('define', 'code', 'member') and line_at(pos) == line_of_last_code:
                items[-1] = items[-1][:-1] + (' '.join(comment_lines(comment)),)
            else:
                items.append(('comment', comment))
            pos = close + 2
            continue
        if text.startswith('#define', pos) or text.startswith('#   define', pos):
            eol = text.find('\n', pos)
            eol = end if eol < 0 else eol
            line = text[pos:eol]
            trailing = ''
            c = line.find('/*')
            if c >= 0:
                trailing = ' '.join(comment_lines(line[c:]))
                line = line[:c]
            m = re.match(r'#\s*define\s+(\w+(?:\([^)]*\))?)\s*(.*)$', line.strip())
            items.append(('define', m.group(1), m.group(2).strip(), trailing))
            line_of_last_code = line_at(pos)
            pos = eol
            continue
        m = re.compile(r'typedef\s+struct\s+(\w+)\s*\{').match(text, pos)
        if m:
            depth = 0
            i = m.end() - 1
            while True:
                if text.startswith('/*', i):
                    i = text.find('*/', i) + 2
                    continue
                if text[i] == '{':
                    depth += 1
                elif text[i] == '}':
                    depth -= 1
                    if depth == 0:
                        break
                i += 1
            body = scan(text, m.end(), i, where + ' ' + m.group(1))
            semi = text.find(';', i)
            items.append(('struct', m.group(1), body))
            line_of_last_code = line_at(semi)
            pos = semi + 1
            continue
        # a declaration : up to its ';' (outside parentheses)
        depth = 0
        i = pos
        while i < end:
            ch = text[i]
            if text.startswith('/*', i):
                i = text.find('*/', i) + 2
                continue
            if ch == '(':
                depth += 1
            elif ch == ')':
                depth -= 1
            elif ch == ';' and depth == 0:
                break
            i += 1
        code = clean_code(re.sub(r'/\*.*?\*/', ' ', text[pos:i], flags=re.S)) + ';'
        kind = 'member' if where.count(' ') else 'code'
        items.append((kind, code, ''))
        line_of_last_code = line_at(i)
        pos = i + 1
    return items


def render_items(items, out, heading_level):
    run = []

    def flush_code():
        if run:
            out.append('```c')
            for code, trailing in run:
                out.append(code + ('    // ' + trailing if trailing else ''))
            out.append('```')
            out.append('')
            run.clear()

    i = 0
    while i < len(items):
        item = items[i]
        if item[0] == 'comment':
            flush_code()
            lines = comment_lines(item[1])
            is_heading = len(lines) == 1 and re.match(r'^-{2,}.*-{2,}$', lines[0].strip())
            if not is_heading and i + 1 < len(items) and items[i + 1][0] == 'struct':
                # what describes a struct goes under its title
                struct = items[i + 1]
                out.append('#' * heading_level + ' ' + struct[1])
                out.append('')
                out.extend(prose(lines))
                out.append('')
                render_items(struct[2], out, heading_level + 1)
                i += 2
                continue
            out.extend(prose(lines))
            out.append('')
        elif item[0] == 'define':
            flush_code()
            rows = []
            while i < len(items) and items[i][0] == 'define':
                rows.append(items[i])
                i += 1
            seen = {}
            for _, name, value, trailing in rows:
                if name in seen:
                    seen[name] = (seen[name][0] if seen[name][0] == value else 'depends on the platform', seen[name][1] or trailing)
                else:
                    seen[name] = (value, trailing)
            out.append('| | Value | |')
            out.append('|---|---|---|')
            for name, (value, trailing) in seen.items():
                v = '`' + value + '`' if value and value != 'depends on the platform' else value
                out.append('| `' + name + '` | ' + v.replace('|', BS + '|') + ' | ' + inline(trailing) + ' |')
            out.append('')
            continue
        elif item[0] in ('code', 'member'):
            run.append((item[1], item[2]))
        elif item[0] == 'struct':
            flush_code()
            out.append('#' * heading_level + ' ' + item[1])
            out.append('')
            render_items(item[2], out, heading_level + 1)
        i += 1
    flush_code()


def interface_of(items):
    """("shard.cvars", "1.0", summary) from the header's first comment, or None for a header that is not an interface"""
    for item in items:
        if item[0] != 'comment':
            continue
        text = ' '.join(comment_lines(item[1]))
        m = re.match(r'Interface "([\w.]+)" (\d+)\.(\d+) : (.*?)(?:\.|$)', text)
        if m:
            return m.group(1), m.group(2) + '.' + m.group(3), m.group(4).split(' (')[0].strip()
        return None
    return None


def first_sentence(items):
    for item in items:
        if item[0] == 'comment':
            text = ' '.join(comment_lines(item[1]))
            text = re.sub(r'^Shard C ABI : ', '', text)
            return re.split(r'(?<=[a-z\)]) ?[.:;] ', text)[0].rstrip('.')
    return ''


def offered_interfaces():
    """{ "SHARD_IFACE_X": (major, minor) } as the host offers them"""
    text = open(HOST_TABLE, encoding='utf-8').read()
    return {m.group(1): (m.group(2), m.group(3)) for m in re.finditer(r'\{\s*(SHARD_IFACE_\w+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,', text)}


# ---------------------------------------------------------------------------------------------------- C++ headers

def strip_strings_and_comments(text):
    """The same text with string / char literals and comments blanked (newlines kept) : for counting braces. /// comments stay visible."""
    out = list(text)
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        if text.startswith('///', i):
            j = text.find('\n', i)
            i = n if j < 0 else j
            continue
        if text.startswith('//', i):
            j = text.find('\n', i)
            j = n if j < 0 else j
            for k in range(i, j):
                out[k] = ' '
            i = j
            continue
        if text.startswith('/*', i):
            j = text.find('*/', i) + 2
            for k in range(i, j):
                if out[k] != '\n':
                    out[k] = ' '
            i = j
            continue
        if c == '"' or c == "'":
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == BS else 1
            for k in range(i + 1, j):
                out[k] = ' '
            i = j + 1
            continue
        i += 1
    return ''.join(out)


def parse_cpp(text):
    """The file comment, then what a plugin uses : documented macros, and the public declarations of the namespace shard (not detail)"""
    lines = text.split('\n')
    header = []
    for l in lines:
        if l.startswith('//'):
            header.append(l[2:][1:] if l[2:].startswith(' ') else l[2:])
        else:
            break
    plain = strip_strings_and_comments(text)

    entries = []        # ('class', name, doc, members) / ('decl', code, doc) / ('macro', signature, doc)
    scopes = []         # [kind, access, entry or None] ; kind : namespace / skip / class / body
    doc = []
    i = 0
    n = len(plain)
    stmt_start = None

    def in_public():
        if not scopes:
            return False
        kinds = [s[0] for s in scopes]
        if 'skip' in kinds or 'body' in kinds:
            return False
        top = scopes[-1]
        return top[0] == 'namespace' or (top[0] == 'class' and top[1] == 'public')

    def record(code, d):
        code = ' '.join(code.split())
        code = re.sub(r'\s*\{\s*$', '', code)
        if not code or not in_public():
            return
        if scopes[-1][0] == 'class':
            cls = scopes[-1][2]
            if cls is None:
                return
            name = cls[1].split('<')[0]
            if re.match(r'^(explicit\s+)?' + re.escape(name) + r'\s*\(', code) or re.match(r'^(virtual\s+)?~', code):
                if not d:
                    return      # constructors and destructors without a word to say
            cls[3].append((code, d))
        else:
            entries.append(('decl', code, d))

    while i < n:
        c = plain[i]
        if stmt_start is None:
            if c.isspace():
                i += 1
                continue
            if plain.startswith('///', i):
                j = plain.find('\n', i)
                j = n if j < 0 else j
                if not plain.startswith('///<', i):      # a ///< belongs to the declaration before it (see below)
                    doc.append(re.sub(r'^@brief\s+', '', text[i + 3:j].strip()))
                i = j
                continue
            if c == '#':
                # a preprocessor line, with its continuations
                j = i
                while True:
                    e = plain.find('\n', j)
                    e = n if e < 0 else e
                    if plain[e - 1:e] == BS:
                        j = e + 1
                        continue
                    break
                m = re.match(r'#\s*define\s+(\w+(?:\([^)]*\))?)', text[i:e])
                if m and doc and not scopes:
                    entries.append(('macro', m.group(1), list(doc)))
                doc = []
                i = e
                continue
            if c == '}':
                scopes.pop()
                i += 1
                while i < n and plain[i] in ' \t':
                    i += 1
                if i < n and plain[i] == ';':
                    i += 1
                doc = []
                continue
            stmt_start = i
            continue
        # inside a statement : up to ';' or '{' outside parentheses, or an access specifier
        depth = 0
        j = stmt_start
        while j < n:
            ch = plain[j]
            if ch == '(' or ch == '[':
                depth += 1
            elif ch == ')' or ch == ']':
                depth -= 1
            elif depth == 0 and ch in ';{':
                break
            elif depth == 0 and ch == ':' and plain[j + 1:j + 2] != ':' and plain[j - 1:j] != ':':
                word = plain[stmt_start:j].strip()
                if word in ('public', 'private', 'protected'):
                    break
            j += 1
        code = re.sub(r'//[^\n]*', '', text[stmt_start:j])
        code_plain = ' '.join(plain[stmt_start:j].split())
        d = list(doc)
        doc = []
        if j < n and plain[j] == ':':
            if scopes and scopes[-1][0] == 'class':
                scopes[-1][1] = code_plain
            i = j + 1
            stmt_start = None
            continue
        if j >= n:
            break
        if plain[j] == ';':
            m = re.match(r'^(template\s*<[^{;]*>\s*)?(class|struct)\s+(\w+)\s*$', code_plain)
            if m:
                # a declared class : its documentation, if it has some
                if d and in_public():
                    entries.append(('class', m.group(3), d, []))
            elif not code_plain.startswith(('static_assert', 'friend')):
                record(code, d)
            # a trailing ///< belongs to what precedes it
            e = text.find('\n', j)
            rest = text[j + 1:e if e >= 0 else n]
            tail = re.search(r'///<\s*(.*)$', rest)
            if tail:
                target = scopes[-1][2][3] if scopes and scopes[-1][0] == 'class' and scopes[-1][2] else None
                if target:
                    target[-1] = (target[-1][0], target[-1][1] + [tail.group(1)])
            i = j + 1
            stmt_start = None
            continue
        # '{'
        m_ns = re.match(r'^namespace\s+(\w+)\s*$', code_plain)
        m_cls = re.match(r'^(template\s*<.*>\s*)?(class|struct)\s+([\w:]+(?:\s*<.*>)?)\s*(final)?\s*(:.*)?$', code_plain)
        m_enum = re.match(r'^enum\s+(class\s+)?(\w+)', code_plain)
        if m_ns:
            scopes.append(['skip' if m_ns.group(1) == 'detail' else 'namespace', 'public', None])
        elif m_cls and '(' not in code_plain.split('<')[0]:
            name = re.sub(r'\s+', '', m_cls.group(3))
            entry = None
            if in_public() and not name.startswith('shard::'):
                base = (' ' + m_cls.group(5).strip()) if m_cls.group(5) else ''
                entry = ('class', name + base, d, [])
                entries.append(entry) if scopes and scopes[-1][0] == 'namespace' else None
                if scopes and scopes[-1][0] == 'class':
                    entry = None    # a nested class : an implementation detail
            scopes.append(['class', 'private' if m_cls.group(2) == 'class' else 'public', entry])
        elif m_enum:
            # the enum and its values, as written
            close = plain.find('}', j)
            body = ' '.join(text[j + 1:close].split())
            record(code_plain + ' { ' + body + ' }', d)
            i = close + 1
            while i < n and plain[i] in ' \t;':
                i += 1
            stmt_start = None
            continue
        else:
            # a function body, or the braces of an initializer : skipped, the declaration recorded
            k = j
            depth = 0
            while k < n:
                if plain[k] == '{':
                    depth += 1
                elif plain[k] == '}':
                    depth -= 1
                    if depth == 0:
                        break
                k += 1
            after = plain[k + 1:].lstrip()
            if '(' not in code_plain.split('=')[0] and after.startswith(';'):
                record(code + text[j:k + 1], d)     # a member with its initializer
                i = k + 1 + (len(plain[k + 1:]) - len(after)) + 1
            else:
                record(code, d)
                i = k + 1
            stmt_start = None
            continue
        i = j + 1
        stmt_start = None
    return header, entries


def merge_specializations(entries):
    """Specializations of one template (CVar<bool>, CVar<float>...) read as one entry"""
    out = []
    for e in entries:
        if e[0] == 'class' and '<' in e[1]:
            base = e[1].split('<')[0]
            prev = next((o for o in out if o[0] == 'class' and o[1].split('<')[0] == base), None)
            if prev is not None:
                # the specializations' names in the title ; the members of the first one stand for all
                prev[4].append(e[1].split(' ')[0])
                if not prev[3]:
                    prev[3] = e[3]
                continue
            out.append(['class', e[1], e[2], e[3], [e[1].split(' ')[0]]])
        elif e[0] == 'class':
            out.append(['class', e[1], e[2], e[3], []])
        else:
            out.append(list(e))
    return out


def render_cpp(header, entries, out):
    # the file comment : prose, and its indented lines as an example
    para = []
    example = []
    for l in header:
        if l.startswith('  ') or (example and not l.strip()):
            example.append(l)
            continue
        if example:
            while example and not example[-1].strip():
                example.pop()
            out.extend(prose(para))
            out.append('')
            para = []
            out.append('```cpp')
            out.extend(e[2:].rstrip() if e.startswith('  ') else e for e in example)
            out.append('```')
            out.append('')
            example = []
        para.append(l)
    if example:
        out.extend(prose(para))
        out.append('')
        para = []
        out.append('```cpp')
        out.extend(e[2:].rstrip() for e in example)
        out.append('```')
        out.append('')
    if para:
        out.extend(prose(para))
        out.append('')

    loose = []

    def flush_loose():
        if loose:
            out.append('```cpp')
            for code, d in loose:
                out.extend('/// ' + x for x in d)
                out.append(code + ('' if code.endswith(';') or code.endswith(')') and code.startswith('SHARD_') else ';'))
            out.append('```')
            out.append('')
            loose.clear()

    for e in merge_specializations(entries):
        if e[0] == 'class':
            flush_loose()
            name = e[1]
            title = name.split(' :')[0]
            if len(e[4]) > 1:
                title = ', '.join(e[4])
            out.append('#### `' + title + '`')
            out.append('')
            if e[2]:
                out.extend(prose(e[2]))
                out.append('')
            if e[3]:
                out.append('```cpp')
                for code, d in e[3]:
                    out.extend('/// ' + x for x in d)
                    out.append(code if code.endswith(';') else code + ';')
                out.append('```')
                out.append('')
        elif e[0] == 'macro':
            loose.append((e[1], e[2]))
        else:
            loose.append((e[1], e[2]))
    flush_loose()


# ---------------------------------------------------------------------------------------------------- the page

def generate():
    out = []
    out.append('<!-- Generated by scripts/gen_api_reference.py from the headers of sdk/include : edit the headers, then run the script. -->')
    out.append('')
    out.append('# API reference')
    out.append('')
    out.append('## The C ABI')
    out.append('')
    out.append('The headers in `sdk/include/shard` are the contract between a plugin and the engine. This page is generated from their comments, '
               'in reading order. See [Plugins](concepts/plugins.md#the-c-abi-and-the-sdk) for the concepts and '
               '[the tutorial](tutorials/write-a-game-with-the-sdk.md) to build a plugin.')
    out.append('')
    out.append('Every function says which thread may call it : `any`, `system` (from inside a system, possibly in parallel with others), '
               '`script` (from a script callback) or `main` (the main thread, outside systems). A function returns a `ShardResult`, either '
               '`SHARD_OK` (0) or a negative error, and the details go to the engine\'s log under the plugin\'s id. Strings are `ShardStr` : '
               'UTF-8, not necessarily zero terminated, valid for the duration of the call. A "text out" function takes '
               '`(buffer, capacity, *size)`, copies what fits and returns the full length.')
    out.append('')

    offered = offered_interfaces()
    sections = []
    summary = []
    for f in ordered(C_DIR, C_ORDER, C_SKIP, '.h'):
        text = open(os.path.join(C_DIR, f), encoding='utf-8').read().replace('\r\n', '\n')
        items = parse_c(text, f)
        iface = interface_of(items)
        if iface:
            define = next((it[1] for it in items if it[0] == 'define' and it[1].startswith('SHARD_IFACE_')), None)
            if not define:
                raise GenError(f + ' announces the interface ' + iface[0] + ' but defines no SHARD_IFACE_ name')
            if define not in offered:
                raise GenError(f + ' : the host does not offer ' + define + ' (' + HOST_TABLE + ')')
            host = offered[define][0] + '.' + offered[define][1]
            if host != iface[1]:
                raise GenError(f + ' says "' + iface[0] + '" ' + iface[1] + ', the host offers ' + host + ' : make them agree')
            summary.append('| [`' + f + '`](#' + f.replace('.', '') + ') | `' + iface[0] + '` ' + iface[1] + ' | ' + inline(iface[2]) + ' |')
        else:
            summary.append('| [`' + f + '`](#' + f.replace('.', '') + ') | | ' + inline(first_sentence(items)) + ' |')
        section = ['### ' + f, '']
        render_items(items, section, 4)
        sections.append(section)

    out.append('| Header | Interface | |')
    out.append('|---|---|---|')
    out.extend(summary)
    out.append('')
    for s in sections:
        out.extend(s)

    out.append('## The C++ SDK')
    out.append('')
    out.append('`sdk/include/shard/cpp` : header-only C++17 over the C ABI, compiled into the plugin. `#include <shard/cpp/plugin.hpp>` brings all '
               'of it. Every callback of the SDK catches what the plugin throws : `shard::Error` (what a registration the host refused throws, '
               'with its `ShardResult`) and anything else become an error of that callback, with the message in the log. Below, the public '
               'declarations of each header (bodies left out).')
    out.append('')
    for f in ordered(CPP_DIR, CPP_ORDER, CPP_SKIP, '.hpp'):
        text = open(os.path.join(CPP_DIR, f), encoding='utf-8').read().replace('\r\n', '\n')
        header, entries = parse_cpp(text)
        out.append('### shard/cpp/' + f)
        out.append('')
        render_cpp(header, entries, out)

    while out and out[-1] == '':
        out.pop()
    return '\n'.join(out) + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--check', action='store_true', help='fail if the page is not up to date')
    parser.add_argument('--out', default=OUT)
    args = parser.parse_args()
    try:
        page = generate()
    except GenError as e:
        print('gen_api_reference : ' + str(e), file=sys.stderr)
        return 2
    if args.check:
        current = open(args.out, encoding='utf-8').read().replace('\r\n', '\n') if os.path.exists(args.out) else ''
        if current != page:
            print(args.out + ' is not what the headers give : run python scripts/gen_api_reference.py', file=sys.stderr)
            return 1
        print(args.out + ' is up to date')
        return 0
    with open(args.out, 'w', encoding='utf-8', newline='\n') as f:
        f.write(page)
    print('wrote ' + args.out)
    return 0


if __name__ == '__main__':
    sys.exit(main())
