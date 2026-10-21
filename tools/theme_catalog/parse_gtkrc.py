#!/usr/bin/env python3
# tools/theme_catalog/parse_gtkrc.py — the GNOME 2 theme formats: a GTK 2 gtkrc (its `gtk-color-scheme`, its styles
# with their colour assignments, style properties and `engine` blocks, and the class / widget bindings) and a
# metacity theme (metacity-theme-1.xml: its draw_ops, each operation's colour spec, the frame styles and the frame
# style sets). Data in, dict out, every file's path and sha256 with it; nothing is evaluated here but the two colour
# grammars, each by the arithmetic of the program that reads it (gtkrc_color: GTK 2's gtkrc.c; metacity_color:
# metacity's theme.c), the primitives toolkit_rules.py's GTK 2 block. A malformed file is a one-line hard fail naming
# it (NO BACKSTOPS: the inputs are pinned third-party files).
import hashlib, re
import xml.etree.ElementTree as ET
import toolkit_rules as T


def die(path, msg):
    raise SystemExit(f'{path}: {msg}')


def sha256(path):
    return hashlib.sha256(open(path, 'rb').read()).hexdigest()


def unhex8(s, path):
    if not re.fullmatch(r'#[0-9a-fA-F]{6}', s): die(path, f'{s!r} is not a #rrggbb colour')
    return tuple(int(s[i:i + 2], 16) for i in (1, 3, 5))


# ------------------------------------------------------------------ gtkrc
_TOKEN = re.compile(r'\s*(?:(?P<str>"(?:[^"\\]|\\.)*")|(?P<word>[A-Za-z0-9_@#.:\-*<>]+)|(?P<punct>[{}\[\]=(),]))')


def _strip_comments(txt):
    """gtkrc comments run from # to the line's end outside a string; a colour inside a string ("#rrggbb") stays."""
    out = []
    for line in txt.splitlines():
        buf, q = [], False
        for i, ch in enumerate(line):
            if ch == '"' and (i == 0 or line[i - 1] != '\\'): q = not q
            if ch == '#' and not q and (i == 0 or not line[i - 1].isalnum()) and not re.match(r'#[0-9a-fA-F]{6}\b', line[i:]):
                break
            buf.append(ch)
        out.append(''.join(buf))
    return '\n'.join(out)


def _tokens(txt, path):
    toks, pos = [], 0
    txt = txt.rstrip()
    while pos < len(txt):
        m = _TOKEN.match(txt, pos)
        if not m or m.end() == pos: die(path, f'unreadable text at {txt[pos:pos + 40]!r}')
        toks.append(m.group('str') or m.group('word') or m.group('punct'))
        pos = m.end()
    return toks


def _unq(t):
    return t[1:-1].encode().decode('unicode_escape') if t.startswith('"') else t


def _expr(toks, i, path):
    """One gtkrc value from toks[i]: a word or string, or a function call `name ( arg , ... )` kept as a tuple
    (name, arg, ...) -> (value, next index)."""
    t = toks[i]
    if i + 1 < len(toks) and toks[i + 1] == '(':
        args, j = [], i + 2
        while True:
            a, j = _expr(toks, j, path)
            args.append(a)
            if toks[j] == ',': j += 1; continue
            if toks[j] == ')': return (t,) + tuple(args), j + 1
            die(path, f'unreadable arguments of {t}')
    if t == '{':                                   # a tuple value: { a, b, ... }
        vals, j = [], i + 1
        while toks[j] != '}':
            if toks[j] != ',': vals.append(_unq(toks[j]))
            j += 1
        return ('{}',) + tuple(vals), j + 1
    return _unq(t), i + 1


def _block(toks, i, path):
    """A `{ ... }` body from toks[i] (just past the brace) -> ({'colors', 'properties', 'engine'}, next index): colour
    assignments `bg[NORMAL] = <expr>`, any other assignment `key = <expr>` (xthickness, GtkButton::..., an engine's
    option), and nested `engine "<name>" { ... }` blocks."""
    body = {'colors': {}, 'properties': {}, 'engines': {}}
    while toks[i] != '}':
        if toks[i] == 'engine':
            name = _unq(toks[i + 1])
            if toks[i + 2] != '{': die(path, f'engine {name!r} has no block')
            sub, i = _block(toks, i + 3, path)
            body['engines'][name] = sub['properties']
            continue
        key = toks[i]; i += 1
        if toks[i] == '[':
            key = f'{key}[{toks[i + 1]}]'
            if toks[i + 2] != ']': die(path, f'unreadable state of {key}')
            i += 3
            if toks[i] != '=': die(path, f'{key}: no =')
            body['colors'][key], i = _expr(toks, i + 1, path)
            continue
        if toks[i] != '=': die(path, f'{key}: no =')
        body['properties'][key], i = _expr(toks, i + 1, path)
    return body, i + 1


def parse_gtkrc(path):
    """-> {'file', 'sha256', 'color_scheme': {name: rgb}, 'styles': {name: {'parent', 'colors', 'properties',
    'engines'}}, 'bindings': [(kind, pattern, style)]}: the gtk-color-scheme's named colours (each "name:#rrggbb"),
    every `style "name" [= "parent"] { ... }`, and every `class` / `widget_class` / `widget` binding in file order."""
    txt = open(path, encoding='utf-8').read()
    toks = _tokens(_strip_comments(txt), path)
    out = {'file': path, 'sha256': sha256(path), 'color_scheme': {}, 'styles': {}, 'bindings': []}
    i = 0
    while i < len(toks):
        t = toks[i]
        if t == 'gtk-color-scheme':
            if toks[i + 1] != '=': die(path, 'gtk-color-scheme: no =')
            for item in _unq(toks[i + 2]).split('\n'):
                if not item.strip(): continue
                name, _, val = item.partition(':')
                out['color_scheme'][name.strip()] = unhex8(val.strip(), path)
            i += 3
        elif t == 'style':
            name, i = _unq(toks[i + 1]), i + 2
            parent = None
            if toks[i] == '=': parent, i = _unq(toks[i + 1]), i + 2
            if toks[i] != '{': die(path, f'style {name!r} has no block')
            body, i = _block(toks, i + 1, path)
            if name in out['styles']: die(path, f'style {name!r} defined twice')
            out['styles'][name] = {'parent': parent, **body}
        elif t in ('class', 'widget_class', 'widget'):
            pat, i = _unq(toks[i + 1]), i + 2
            if toks[i] != 'style': die(path, f'{t} {pat!r}: no style')
            i += 1
            if toks[i] == ':': i += 2                         # a priority (": highest")
            elif toks[i].startswith(':'): i += 1
            out['bindings'].append((t, pat, _unq(toks[i]))); i += 1
        else:
            die(path, f'unexpected {t!r}')
    if len(out['color_scheme']) != 8: die(path, f'the gtk-color-scheme names {len(out["color_scheme"])} colours, not 8')
    return out


def gtkrc_color(expr, scheme, path='gtkrc'):
    """A gtkrc colour value -> a GdkColor (16-bit channels) by GTK 2's own arithmetic (gtkrc.c
    gtk_rc_parse_color_full): `@name` the scheme's colour (each byte replicated, T.gdk_color); "#rrggbb" the same;
    `shade (k, c)` gtk_style_shade on c (T.gtk2_shade in doubles over c / 65535, stored truncated, T.gdk16);
    `darker (c)` / `lighter (c)` that shade at 0.7 / 1.3 (gtkstyle.c's DARKNESS_MULT / LIGHTNESS_MULT); `mix (f, a, b)`
    a x f + b x (1 - f) per 16-bit channel, truncated."""
    if isinstance(expr, str):
        if expr.startswith('@'):
            if expr[1:] not in scheme: die(path, f'no colour {expr}')
            return T.gdk_color(scheme[expr[1:]])
        return T.gdk_color(unhex8(expr, path))
    f, *args = expr
    shade16 = lambda c, k: T.gdk16(T.gtk2_shade(tuple(v / 65535.0 for v in c), k))
    if f == 'shade': return shade16(gtkrc_color(args[1], scheme, path), float(args[0]))
    if f == 'darker': return shade16(gtkrc_color(args[0], scheme, path), 0.7)
    if f == 'lighter': return shade16(gtkrc_color(args[0], scheme, path), 1.3)
    if f == 'mix':
        k, a, b = float(args[0]), gtkrc_color(args[1], scheme, path), gtkrc_color(args[2], scheme, path)
        return tuple(int(k * x + (1.0 - k) * y) for x, y in zip(a, b))
    die(path, f'unknown colour function {f!r}')


def expr_text(expr):
    """A parsed gtkrc value back as gtkrc text (the record's spelling)."""
    if isinstance(expr, str): return expr
    f, *args = expr
    if f == '{}': return '{ ' + ', '.join(args) + ' }'
    return f'{f} (' + ', '.join(expr_text(a) for a in args) + ')'


# ------------------------------------------------------------------ metacity
def parse_metacity_color(spec, path):
    """A metacity colour spec (theme.c meta_color_spec_new_from_string) -> a tuple: ('gtk', component, state) for
    "gtk:bg[NORMAL]", ('shade', base, k) for "shade/<base>/<k>", ('blend', background, foreground, alpha) for
    "blend/<background>/<foreground>/<alpha>" (the FIRST colour the background), ('rgb', (r, g, b)) for "#rrggbb"."""
    m = re.fullmatch(r'gtk:(fg|bg|base|text|light|dark|mid|text_aa)\[([A-Z]+)\]', spec)
    if m: return ('gtk', m.group(1), m.group(2))
    if spec.startswith('#'): return ('rgb', unhex8(spec, path))
    if spec.startswith('shade/'):
        base, k = spec[len('shade/'):].rsplit('/', 1)
        return ('shade', parse_metacity_color(base, path), float(k))
    if spec.startswith('blend/'):
        parts = spec[len('blend/'):].split('/')
        if len(parts) != 3: die(path, f'unreadable blend {spec!r}')
        return ('blend', parse_metacity_color(parts[0], path), parse_metacity_color(parts[1], path), float(parts[2]))
    die(path, f'unreadable colour {spec!r}')


def metacity_color(spec, gtk_colors):
    """A parsed metacity colour spec -> a GdkColor, by metacity's own arithmetic (theme.c meta_color_spec_render):
    a GTK colour from `gtk_colors` {(component, state): GdkColor}; shade = gtk_style_shade (T.gtk2_shade over
    c / 65535.0, stored x 65535.0 truncated); blend = color_composite (T.metacity_blend)."""
    kind = spec[0]
    if kind == 'gtk': return gtk_colors[(spec[1], spec[2])]
    if kind == 'rgb': return T.gdk_color(spec[1])
    if kind == 'shade': return T.gdk16(T.gtk2_shade(tuple(v / 65535.0 for v in metacity_color(spec[1], gtk_colors)), spec[2]))
    if kind == 'blend': return T.metacity_blend(metacity_color(spec[1], gtk_colors), metacity_color(spec[2], gtk_colors), spec[3])
    raise AssertionError(spec)


def parse_metacity(path):
    """-> {'file', 'sha256', 'info': {tag: text}, 'constants': {name: value}, 'draw_ops': {name: [op]},
    'frame_styles': {name: {'geometry', 'parent', 'pieces': {position: draw_ops}, 'buttons': {(function, state):
    draw_ops}}}, 'frame_style_sets': {name: {(focus, state): style}}}; each op a dict of the element's tag and its
    attributes, a `color` attribute (or a gradient's <color value>s, `colors`) parsed by parse_metacity_color."""
    try:
        root = ET.parse(path).getroot()
    except ET.ParseError as e:
        die(path, f'not XML: {e}')
    if root.tag != 'metacity_theme': die(path, f'root is <{root.tag}>, not <metacity_theme>')
    out = {'file': path, 'sha256': sha256(path), 'info': {}, 'constants': {}, 'draw_ops': {}, 'frame_styles': {},
           'frame_style_sets': {}}
    info = root.find('info')
    if info is not None: out['info'] = {c.tag: (c.text or '').strip() for c in info}
    for c in root.findall('constant'): out['constants'][c.get('name')] = c.get('value')
    for d in root.findall('draw_ops'):
        ops = []
        for op in d:
            rec = {'op': op.tag, **{k: v for k, v in op.attrib.items() if k != 'color'}}
            if 'color' in op.attrib: rec['color'] = parse_metacity_color(op.get('color'), path)
            if op.tag == 'gradient': rec['colors'] = [parse_metacity_color(c.get('value'), path) for c in op.findall('color')]
            ops.append(rec)
        out['draw_ops'][d.get('name')] = ops
    for f in root.findall('frame_style'):
        out['frame_styles'][f.get('name')] = {
            'geometry': f.get('geometry'), 'parent': f.get('parent'),
            'pieces': {p.get('position'): p.get('draw_ops') for p in f.findall('piece')},
            'buttons': {(b.get('function'), b.get('state')): b.get('draw_ops') for b in f.findall('button')}}
    for s in root.findall('frame_style_set'):
        out['frame_style_sets'][s.get('name')] = {(fr.get('focus'), fr.get('state')): fr.get('style') for fr in s.findall('frame')}
    return out


def frame_piece(theme, style, position):
    """The draw_ops name a frame style paints `position` with, through its parents (metacity's inheritance)."""
    while style is not None:
        fs = theme['frame_styles'][style]
        if position in fs['pieces']: return fs['pieces'][position]
        style = fs['parent']
    raise SystemExit(f'{theme["file"]}: no {position} piece')


def draw_ops_flat(theme, name):
    """A draw_ops' operations with every <include> expanded in place (metacity draws an include's ops at its seat)."""
    out = []
    for op in theme['draw_ops'][name]:
        if op['op'] == 'include': out += draw_ops_flat(theme, op['name'])
        else: out.append(op)
    return out
