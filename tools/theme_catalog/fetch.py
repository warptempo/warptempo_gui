#!/usr/bin/env python3
# tools/theme_catalog/fetch.py — the pinned sources (sources.py) -> tmp/theme_sources/<id>/ (git-ignored, never
# committed). Each source's file list is written to tmp/theme_sources/<id>/MANIFEST.json (a directory listed at the
# pinned commit), which build.py reads. A file already on disk is kept (the commit pins its bytes); --refresh fetches
# again. Any failure is one line naming the file and exit 1: the inputs are pinned third-party files, no recovery.
#
#   python3 tools/theme_catalog/fetch.py [--refresh]
import json, os, shutil, subprocess, sys, urllib.request
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from sources import SOURCES, SOURCES_DIR, REPO, GH, TDE, SF, IMAGE, file_url, local_path


def die(msg):
    raise SystemExit(f'fetch: {msg}')


def get(url):
    req = urllib.request.Request(url, headers={'User-Agent': 'warptempo-theme-catalog'})
    try:
        with urllib.request.urlopen(req, timeout=60) as r: return r.read()
    except Exception as e:
        die(f'{url}: {e}')


def listing(src):
    """The source's files: its explicit list, or its directory listed at the pinned commit, filtered by suffix."""
    s = SOURCES[src]
    if 'files' in s: return list(s['files'])
    if s['host'] == GH:
        tree = json.loads(get(f"https://api.github.com/repos/{s['repo']}/git/trees/{s['commit']}?recursive=1"))
        if tree.get('truncated'): die(f"{s['repo']}@{s['commit']}: the tree listing is truncated")
        pre = s['dir'] + '/' if s['dir'] else ''
        names = [t['path'][len(pre):] for t in tree['tree'] if t['type'] == 'blob' and t['path'].startswith(pre)
                 and '/' not in t['path'][len(pre):] and t['path'].endswith(s['suffix'])]
    elif s['host'] == TDE:
        api = f"https://mirror.git.trinitydesktop.org/gitea/api/v1/repos/{s['repo']}/contents/{s['dir']}?ref={s['commit']}"
        names = [t['name'] for t in json.loads(get(api)) if t['type'] == 'file' and t['name'].endswith(s['suffix'])]
    else:
        die(f'{src}: a listing needs a git host')
    return sorted(os.path.join(s['dir'], n) if s['dir'] else n for n in names)


def fetch_image(src, files, refresh):
    """unsquashfs -e each file out of the image's squashfs (unpacked from the ISO with bsdtar when missing)."""
    s = SOURCES[src]
    sq = os.path.join(REPO, s['unpacked'])
    if not os.path.exists(sq):
        iso = os.path.join(REPO, s['image'])
        if not os.path.exists(iso): die(f"{iso}: the image is missing")
        os.makedirs(os.path.dirname(sq), exist_ok=True)
        subprocess.run(['bsdtar', '-xf', iso, '-C', os.path.dirname(os.path.dirname(sq)), s['squashfs']], check=True)
    out = os.path.join(SOURCES_DIR, src)
    for f in files:
        dst = local_path(src, f)
        if os.path.exists(dst) and not refresh: continue
        r = subprocess.run(['unsquashfs', '-q', '-n', '-f', '-d', out, sq, '-e', f"{s['dir']}/{f}"],
                           capture_output=True, text=True)
        if r.returncode or not os.path.exists(dst): die(f"{sq}:/{s['dir']}/{f}: unsquashfs failed {r.stderr.strip()}")


def main():
    refresh = '--refresh' in sys.argv
    for src, s in SOURCES.items():
        d = os.path.join(SOURCES_DIR, src); os.makedirs(d, exist_ok=True)
        files = listing(src)
        if s['host'] == IMAGE:
            fetch_image(src, files, refresh)
        else:
            for f in files:
                dst = local_path(src, f)
                if os.path.exists(dst) and not refresh: continue
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                data = get(file_url(src, f))
                open(dst, 'wb').write(data)
        json.dump({'source': src, 'commit': s.get('commit'), 'image': s.get('image'), 'files': files},
                  open(os.path.join(d, 'MANIFEST.json'), 'w'), indent=1)
        print(f'{src:18s} {len(files):3d} files  {s.get("commit") or s.get("image")}')


if __name__ == '__main__':
    main()
