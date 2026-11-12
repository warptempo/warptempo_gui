#!/usr/bin/env python3
"""THE THEME CONVERTER (architect 2026-11-11, session 110, THE THEME FILES):
an Ableton Live theme file (`.ask`, Ableton's theme XML) -> our own keyed
theme file, `<slug>.theme`, the format src/gui/theme_roles.h's head owns.

A standalone tool, stdlib only, with no link path from any product target.
It is run ONCE per theme on the laptop; the repository's themes/system/ and
themes/extra/ are its output, and no .ask file enters the repository or the
APK.

    python3 -I tools/themes/ask_to_theme.py [options] OUT_DIR FILE.ask...

For each FILE.ask it writes OUT_DIR/<slug>.theme, the slug the file's stem
through THE SLUG RULE (theme_slug_of_stem, src/gui/gui_theme.h): a short
header comment (the source file's name, "Ableton Live theme XML", this
tool's name), then one `Role=HEX` line per role of THE ROLE TABLE, in the
table's order. THE ROLE NAMES ARE READ OUT OF THE C++ TABLE, kThemeRoles in
src/gui/theme_roles.cpp, so that initializer stays the one enumeration.

THE READ is exactly the reader the app used while it read .ask files itself
(2026-11-06 .. 11-11):
  - THE SCANNER: every `<Name Value="#hex" />` of the text in order -- a
    name of [A-Za-z0-9_]+, at least one space (space, tab, LF, CR, FF, VT),
    `Value="#`, exactly 6 or 8 hex digits, `"`, any spaces, `/>` --
    non-overlapping, left to right; where a `<` starts no such tag the scan
    resumes one byte after it.
  - A FLAT ROLE is the FIRST tag of its name anywhere in the file.
  - THE ONE NESTED ROLE, written under the key `StandardVuMeter/Maximum`, is
    the first `Maximum` tag inside the first `<StandardVuMeter>` ...
    `</StandardVuMeter>` block (a `Maximum` stands in every VU meter block,
    so it is never read flat).
  - The hex digits are uppercased; an 8-digit word's alpha byte is KEPT, raw
    (the app's derivation composites it).
A role missing is an error: the run exits non-zero naming the file and the
role, and writes nothing for that file.

OPTIONS, for the files the app ships modified (a `_mod` theme), so that every
role line is still this tool's output:
  --slug SLUG       write OUT_DIR/SLUG.theme in place of the stem's slug
                    (one FILE.ask only).
  --set ROLE=HEX    replace that role's word (6 or 8 hex digits) after the
                    read; repeatable.
  --note TEXT       one more header comment line, `# TEXT`, after the
                    standard header; repeatable, in order.
"""

import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ROLE_TABLE_SRC = os.path.join(REPO, 'src', 'gui', 'theme_roles.cpp')
NESTED_KEY = 'StandardVuMeter/Maximum'
NESTED_OPEN = '<StandardVuMeter>'
NESTED_CLOSE = '</StandardVuMeter>'
NESTED_ROLE = 'Maximum'
SPACE = ' \t\n\r\f\v'
HEX = set('0123456789abcdefABCDEF')
NAME = set('abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_')


def die(msg):
    sys.stderr.write('ask_to_theme: %s\n' % msg)
    sys.exit(1)


def role_table():
    """kThemeRoles' names, in order, out of the C++ initializer."""
    with open(ROLE_TABLE_SRC, encoding='utf-8') as f:
        text = f.read()
    m = re.search(r'kThemeRoles\[\]\s*=\s*\{(.*?)\n\};', text, re.S)
    if not m:
        die('no kThemeRoles initializer in %s' % ROLE_TABLE_SRC)
    names = re.findall(r'"([^"]*)"', m.group(1))
    if not names or len(set(names)) != len(names):
        die('the kThemeRoles initializer in %s is empty or repeats a name'
            % ROLE_TABLE_SRC)
    return names


def slug_of_stem(stem):
    """THE SLUG RULE (gui_theme.h's theme_slug_of_stem), byte for byte."""
    out = []
    pending = False
    for ch in stem:
        c = ch.lower() if 'A' <= ch <= 'Z' else ch
        if not ('a' <= c <= 'z' or '0' <= c <= '9'):
            pending = True
            continue
        if pending and out:
            out.append('_')
        pending = False
        out.append(c)
    return ''.join(out)


def scan_roles(text):
    """THE SCANNER (the docstring): yields (name, WORD) in order."""
    n = len(text)
    i = 0
    while True:
        i = text.find('<', i)
        if i < 0:
            return
        p = i + 1
        name_at = p
        while p < n and text[p] in NAME:
            p += 1
        name_len = p - name_at
        space_at = p
        while p < n and text[p] in SPACE:
            p += 1
        ok = name_len > 0 and p > space_at and text[p:p + 8] == 'Value="#'
        hex_at = hex_len = 0
        if ok:
            p += 8
            hex_at = p
            while p < n and text[p] in HEX:
                p += 1
            hex_len = p - hex_at
            ok = hex_len in (6, 8) and p < n and text[p] == '"'
        if ok:
            p += 1
            while p < n and text[p] in SPACE:
                p += 1
            ok = text[p:p + 2] == '/>'
        if not ok:
            i += 1
            continue
        p += 2
        yield text[name_at:name_at + name_len], text[hex_at:hex_at + hex_len].upper()
        i = p


def read_ask(path, roles):
    """The file's words by role name (the docstring's THE READ)."""
    with open(path, 'rb') as f:
        text = f.read().decode('latin-1')
    flat = [r for r in roles if r != NESTED_KEY]
    words = {}
    for name, word in scan_roles(text):
        if name in flat and name not in words:
            words[name] = word
    if NESTED_KEY in roles:
        start = text.find(NESTED_OPEN)
        if start >= 0:
            body = start + len(NESTED_OPEN)
            close = text.find(NESTED_CLOSE, body)
            if close >= 0:
                for name, word in scan_roles(text[body:close]):
                    if name == NESTED_ROLE:
                        words[NESTED_KEY] = word
                        break
    for r in roles:
        if r not in words:
            die('%s: missing role %s' % (path, r))
    return words


def main(argv):
    slug_opt = None
    sets = []
    notes = []
    args = []
    i = 0
    while i < len(argv):
        a = argv[i]
        if a in ('--slug', '--set', '--note'):
            if i + 1 >= len(argv):
                die('%s needs a value' % a)
            v = argv[i + 1]
            if a == '--slug':
                slug_opt = v
            elif a == '--set':
                sets.append(v)
            else:
                notes.append(v)
            i += 2
            continue
        if a in ('-h', '--help'):
            sys.stdout.write(__doc__)
            return 0
        args.append(a)
        i += 1
    if len(args) < 2:
        die('usage: ask_to_theme.py [--slug SLUG] [--set ROLE=HEX]... '
            '[--note TEXT]... OUT_DIR FILE.ask...')
    out_dir, files = args[0], args[1:]
    if not os.path.isdir(out_dir):
        die('no folder %s' % out_dir)
    if slug_opt is not None and len(files) != 1:
        die('--slug takes one FILE.ask')
    roles = role_table()
    overrides = {}
    for s in sets:
        m = re.fullmatch(r'([^=]+)=([0-9A-Fa-f]{6}|[0-9A-Fa-f]{8})', s)
        if not m or m.group(1) not in roles:
            die('--set %s: not ROLE=HEX with a role of the table' % s)
        overrides[m.group(1)] = m.group(2).upper()
    for path in files:
        stem = os.path.basename(path)
        if stem.lower().endswith('.ask'):
            stem = stem[:-4]
        slug = slug_opt if slug_opt is not None else slug_of_stem(stem)
        if not slug or slug_of_stem(slug) != slug:
            die('%s: the slug %r is not a slug' % (path, slug))
        words = read_ask(path, roles)
        words.update(overrides)
        lines = ['# %s -- Ableton Live theme XML, converted by '
                 'tools/themes/ask_to_theme.py' % os.path.basename(path)]
        lines += ['# %s' % n if n else '#' for n in notes]
        lines += ['%s=%s' % (r, words[r]) for r in roles]
        out = os.path.join(out_dir, slug + '.theme')
        with open(out, 'w', encoding='utf-8', newline='\n') as f:
            f.write('\n'.join(lines) + '\n')
        sys.stdout.write('%s -> %s\n' % (path, out))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
