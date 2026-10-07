#!/usr/bin/env python3
# tools/mockup/themes.py — THE THEME GRAMMAR, read off the app's own source so the tool cannot drift from it.
#
# The built-in's 40 values, the twenty named colours, the flat-caption pairs, the follower pair and the built-in's
# key are all parsed out
# of src/gui/theme_file.h in the working tree on every run (the constants-from-source discipline of
# tools/palette/common.py's face_metrics). A .theme file is read under the app's own rules (theme_file.cpp's
# read_theme_file and read_theme_folder's stem checks, the shared scanner's lexical contract in
# src/parser/settings_file.cpp): LF-terminated role=value lines split at the first '=', no blank line, no comment,
# no whitespace tolerance, no duplicate; a role the file does not name takes the built-in's value; a caption start
# named without its gradient end gets the end equal to the start; the follower pair's leader named without its
# follower (dk_shadow / flag_outline) gets the follower equal to it. Every refusal is the app's own sentence.
import os, re

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, '..', '..'))
THEME_FILE_H = os.path.join(REPO, 'src', 'gui', 'theme_file.h')


class ThemeError(Exception):
    """A refusal, worded as the app words it."""


def _source():
    return open(THEME_FILE_H).read()


def _block(src, start_pattern):
    m = re.search(start_pattern + r'(.*?)\n\};', src, re.S)
    if not m:
        raise SystemExit(f'tools/mockup: {THEME_FILE_H}: could not find {start_pattern!r} (the source moved)')
    return m.group(1)


def _load():
    src = _source()
    roles = [(n, int(v, 16)) for n, v in re.findall(
        r'\{"(\w+)",\s*&GuiPalette::\w+,\s*0x([0-9A-Fa-f]{6})\}', _block(src, r'kGuiThemeRoles\[\] = \{'))]
    named = {n: int(v, 16) for n, v in re.findall(
        r'\{"(\w+)",\s*0x([0-9A-Fa-f]{6})\}', _block(src, r'kNamedThemeColours\[\] = \{'))}
    pairs = re.findall(r'\{theme_role_index\("(\w+)"\),\s*theme_role_index\("(\w+)"\)\}',
                       _block(src, r'kGuiThemeCaptionGradients\[\] = \{'))
    key = re.search(r'kBuiltinThemeKey = "([^"]+)"', src).group(1)
    follows = re.findall(r'\{theme_role_index\("(\w+)"\),\s*theme_role_index\("(\w+)"\)\}',
                         _block(src, r'kGuiThemeFollowers\[\] = \{'))
    if len(roles) != 40 or len(named) != 20 or len(pairs) != 2 or len(follows) != 1:
        raise SystemExit(f'tools/mockup: {THEME_FILE_H}: read {len(roles)} roles, {len(named)} named colours, '
                         f'{len(pairs)} caption pairs, {len(follows)} follower pairs (expected 40, 20, 2, 1: the '
                         f'source moved)')
    return roles, named, pairs, follows, key


ROLE_TABLE, NAMED, CAPTION_PAIRS, FOLLOWERS, BUILTIN_KEY = _load()
ROLES = [n for n, _ in ROLE_TABLE]


def rgb(word):
    return (word >> 16 & 255, word >> 8 & 255, word & 255)


def hexs(c):
    return '#%02X%02X%02X' % tuple(c)


def colour_word(v):
    """theme_colour_word: one of the twenty names (lowercase) or '#' and six hex digits, either case; else None."""
    if v in NAMED:
        return NAMED[v]
    if len(v) != 7 or v[0] != '#' or any(c not in '0123456789abcdefABCDEF' for c in v[1:]):
        return None
    return int(v[1:], 16)


def is_key_spelling(v):
    """is_theme_key_spelling: runs of [a-z0-9] joined by single hyphens."""
    return re.fullmatch(r'[a-z0-9]+(-[a-z0-9]+)*', v) is not None


def builtin():
    """The built-in theme as {role: (r, g, b)}."""
    return {n: rgb(w) for n, w in ROLE_TABLE}


INVALID_VALUE = "must be #rrggbb or one of the twenty Windows colour names"


def read_theme(path):
    """A .theme file -> {role: (r, g, b)}, every role filled. Refuses as read_theme_folder does, first error only."""
    name = os.path.basename(path)
    head = f"invalid theme file '{path}': "
    if not name.endswith('.theme') or len(name) <= len('.theme'):
        raise ThemeError(head + 'the name must be <key>.theme, the key lowercase letters and digits in runs '
                                'joined by single hyphens')
    key = name[:-len('.theme')]
    if not is_key_spelling(key):
        raise ThemeError(head + 'the name must be <key>.theme, the key lowercase letters and digits in runs '
                                'joined by single hyphens')
    if key == BUILTIN_KEY:
        raise ThemeError(head + f'{BUILTIN_KEY} is the built-in theme and takes no file')
    try:
        data = open(path, 'rb').read()
    except OSError:
        raise ThemeError(head + 'could not open the file')
    words = dict(ROLE_TABLE)
    named, seen = set(), set()
    lines = data.split(b'\n')
    if lines and lines[-1] == b'':
        lines.pop()                                   # std::getline: a final LF ends the last line, no empty line
    for ln, raw in enumerate(lines, 1):
        line = raw.decode('latin-1')
        eq = line.find('=')
        if eq < 0:
            raise ThemeError(head + f'line {ln}: not a key=value line')
        role, value = line[:eq], line[eq + 1:]
        if not role:
            raise ThemeError(head + f'line {ln}: empty key')
        if role in seen:
            raise ThemeError(head + f"line {ln}: duplicate key '{role}'")
        seen.add(role)
        if role not in words:
            raise ThemeError(head + f"line {ln}: unknown role '{role}'")
        w = colour_word(value)
        if w is None:
            raise ThemeError(head + f"line {ln}: role '{role}' has invalid value '{value}': {INVALID_VALUE}")
        words[role] = w
        named.add(role)
    for start, end in CAPTION_PAIRS:                  # THE FLAT CAPTION (theme_file.h's head)
        if start in named and end not in named:
            words[end] = words[start]
    for leader, follower in FOLLOWERS:                # THE FOLLOWERS (the same head)
        if leader in named and follower not in named:
            words[follower] = words[leader]
    return {n: rgb(w) for n, w in words.items()}


def load(spec):
    """'builtin' (or the built-in's key) -> the built-in; anything else is a .theme file path."""
    if spec in ('builtin', BUILTIN_KEY):
        return builtin()
    return read_theme(spec)


def parse_extras(items, allowed):
    """--extra key=value ... -> {key: (r, g, b)}, each value under the one colour grammar, each key one the chrome
    vocabulary names (`allowed`: {key: what it is})."""
    out = {}
    for it in items or []:
        if '=' not in it:
            raise ThemeError(f"--extra '{it}': not a key=value pair")
        k, v = it.split('=', 1)
        if k not in allowed:
            names = ', '.join(sorted(allowed)) or 'none'
            raise ThemeError(f"--extra '{k}': not a key of this chrome vocabulary (its keys: {names})")
        if k in out:
            raise ThemeError(f"--extra '{k}': given twice")
        w = colour_word(v)
        if w is None:
            raise ThemeError(f"--extra '{k}' has invalid value '{v}': {INVALID_VALUE}")
        out[k] = rgb(w)
    return out
