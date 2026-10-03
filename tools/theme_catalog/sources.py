#!/usr/bin/env python3
# tools/theme_catalog/sources.py — THE PINNED SOURCES, one row per fetched set (architect 2026-10-03: every colour of
# a theme is a recorded byte with its provenance). Each row names the project, the repository and the commit it is
# read at, and the files: an explicit list, or a directory and a suffix that fetch.py lists AT THE PINNED COMMIT (a
# listing at a commit is as fixed as the commit). (A local disc image was a host too while the Q4OS 6.9 TDE image was
# a source, until 2026-10-03: no entry depended on it, its six schemes being not imported, so it and the image host
# were dropped.) fetch.py writes each file to
# tmp/theme_sources/<id>/<path>, git-ignored and never committed; the catalog carries the bytes and these rows.
import os

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
SOURCES_DIR = os.path.join(REPO, 'tmp', 'theme_sources')

GH = 'github'          # raw.githubusercontent.com/<repo>/<commit>/<path>; listing by the git trees API
TDE = 'tde-gitea'      # mirror.git.trinitydesktop.org/gitea/<repo>/raw/commit/<commit>/<path>; listing by its contents API
SF = 'sourceforge'     # sourceforge.net/p/<repo>/ci/<commit>/tree/<path>?format=raw

SOURCES = {
    # ---- the colour data
    'reactos': dict(host=GH, project='ReactOS', repo='reactos/reactos',
                    commit='d004b2c119ef54a8cd292801d35654cec49195ca', files=['boot/bootdata/hivedef.inf']),
    'xp_classic_zkedem': dict(host=GH, project='windows10-classic-themes (Ziv Kedem): the Windows XP (WEPOS 2009) '
                              'classic schemes saved from Display Properties as .theme files',
                              repo='zkedem/windows10-classic-themes',
                              commit='f126b0b138cb215f7fd5dfa5ab854edcde94ab72', dir='', suffix='.theme'),
    'xp_classic_8': dict(host=GH, project='1j01/98, desktop/Themes/classicthemes8 (windows8themes.ms): the classic '
                         'schemes as Windows 8 .theme files', repo='1j01/98',
                         commit='5245105214cee90ab984fc15bece6c92db15ca7d',
                         dir='desktop/Themes/classicthemes8/Themes', suffix='.theme'),
    'win98_themes': dict(host=GH, project='1j01/98, desktop/Themes/Windows Official: the Windows 98 / Plus! desktop '
                         'themes', repo='1j01/98', commit='5245105214cee90ab984fc15bece6c92db15ca7d',
                         dir='desktop/Themes/Windows Official', suffix='.theme'),
    'tde_kcs': dict(host=TDE, project='Trinity Desktop (TDE) tdebase, the KDE 3 colour schemes', repo='TDE/tdebase',
                    commit='d66254bc297b4df34480e6e6cfc7da4bda9e8518', dir='kcontrol/krdb/kcs', suffix='.kcsrc'),
    'cde_palettes': dict(host=GH, project='CDE (cdesktopenv, Thomas Adam\'s mirror), the dtstyle palettes',
                         repo='ThomasAdam/cdesktopenv', commit='9b1f60a7135fbe0d549d49735a91d2d9d35707b9',
                         dir='cde/programs/palettes', suffix='.dp'),
    # ---- the toolkit rules and the colour-set roles (read, cited at toolkit_rules.py and roles.py; never parsed)
    'motif_rules': dict(host=SF, project='Motif (The Open Group)', repo='motif/code',
                        commit='0f556b0873c72ba1152a12fd54c3198ee039e413',
                        files=['lib/Xm/Color.c', 'lib/Xm/ColorP.h', 'lib/Xm/ColorObj.c', 'lib/Xm/Xm.h.in']),
    'cde_rules': dict(host=GH, project='CDE (cdesktopenv)', repo='ThomasAdam/cdesktopenv',
                      commit='9b1f60a7135fbe0d549d49735a91d2d9d35707b9',
                      files=['cde/programs/dtsession/SrvPalette.c', 'cde/programs/dtsession/SrvFile_io.c',
                             'cde/programs/dtsession/Srv.h', 'cde/programs/dtwm/WmResource.c',
                             'cde/programs/dtwm/Dtwm.defs.src', 'cde/programs/dtstyle/dtstyle.man']),
    'tde_rules': dict(host=TDE, project='Trinity Desktop (TDE) tdelibs', repo='TDE/tdelibs',
                      commit='46c1092abfc1264b3df9fb51fe166dfd70cc420f',
                      files=['tdecore/tdeapplication.cpp', 'tdecore/tdeglobalsettings.cpp']),
    'wine_rules': dict(host=GH, project='Wine (wine-mirror), shlwapi: ColorRGBToHLS / ColorHLSToRGB, Windows\' '
                       '240-scale integer HLS (the flags\' windows-dialog rule)', repo='wine-mirror/wine',
                       commit='b073859675060c9211fcbccfd90e4e87520dc2c2', files=['dlls/shlwapi/ordinal.c']),   # the wine-10.0 tag
    'tqt_rules': dict(host=TDE, project='Trinity Qt 3 (TQt)', repo='TDE/tqt',
                      commit='029f5d5058ffe10de206047602d91b1dbba4cf50',
                      files=['src/kernel/tqcolor.cpp', 'src/kernel/tqpalette.cpp']),
}


def file_url(src, path):
    """The fetchable URL of one file of a pinned source."""
    s = SOURCES[src]; q = path.replace(' ', '%20')
    if s['host'] == GH: return f"https://raw.githubusercontent.com/{s['repo']}/{s['commit']}/{q}"
    if s['host'] == TDE: return f"https://mirror.git.trinitydesktop.org/gitea/{s['repo']}/raw/commit/{s['commit']}/{q}"
    assert s['host'] == SF, src
    return f"https://sourceforge.net/p/{s['repo']}/ci/{s['commit']}/tree/{q}?format=raw"


def local_path(src, path):
    """Where fetch.py put one file: tmp/theme_sources/<src>/<path>."""
    return os.path.join(SOURCES_DIR, src, path)


def provenance(src, path):
    """The provenance record of one file: project, file path, repository, commit and URL."""
    s = SOURCES[src]
    return {'project': s['project'], 'file': path, 'repository': s['repo'], 'commit': s['commit'],
            'url': file_url(src, path)}

# THE APP'S OWN ENTRY reads src/gui/render.h at this commit (git show, read-only): the constants of the look the app
# paints on 2026-10-03, so the catalog's bytes never move with a later commit.
APP = dict(project='Warptempo GUI', file='src/gui/render.h', commit='da0b10513314bde5a5a06307197b2ca9d1517f44')
