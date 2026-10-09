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
IA = 'archive.org'     # archive.org/download/<item>/<image>/<path, each '/' as %2F>: one file read out of a disc image

SOURCES = {
    # ---- the colour data
    'reactos': dict(host=GH, project='ReactOS', repo='reactos/reactos',
                    commit='d004b2c119ef54a8cd292801d35654cec49195ca', files=['boot/bootdata/hivedef.inf']),
    # Windows 2000's own default-user hive (architect 2026-10-06, the Windows 2000 chrome): its
    # [Control Panel\Colors] is Windows 2000's default scheme, "Windows Standard" (the hive's Appearance\Schemes value
    # of that name carries the same 29 colours); its "Windows Classic" value corroborates windows-98-standard.
    'win2000_hivedef': dict(host=IA, project='Windows 2000 Professional SP3 (Microsoft), the setup\'s default-user '
                            'hive, read as data from the retail disc image',
                            item='win_2000_professional_sp3_english_202605', image='Windows2000ProfessionalSP3.iso',
                            image_sha1='51b9a010af4b6243ff1ccbc4b4fb41725240f17c',
                            files=['I386/HIVEDEF.INF'],
                            sha1={'I386/HIVEDEF.INF': '4cd963825ea971ae1d667c593071c1d62964d050'}),
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
    if s['host'] == IA: return f"https://archive.org/download/{s['item']}/{s['image']}/{q.replace('/', '%2F')}"
    assert s['host'] == SF, src
    return f"https://sourceforge.net/p/{s['repo']}/ci/{s['commit']}/tree/{q}?format=raw"


def local_path(src, path):
    """Where fetch.py put one file: tmp/theme_sources/<src>/<path>."""
    return os.path.join(SOURCES_DIR, src, path)


def provenance(src, path):
    """The provenance record of one file: project, file path, repository, commit and URL (a disc image's file: its item,
    image, the image's SHA-1 and the file's own in place of the repository and the commit)."""
    s = SOURCES[src]
    if s['host'] == IA:
        return {'project': s['project'], 'file': path, 'item': s['item'], 'image': s['image'],
                'image_sha1': s['image_sha1'], 'sha1': s['sha1'][path], 'url': file_url(src, path)}
    return {'project': s['project'], 'file': path, 'repository': s['repo'], 'commit': s['commit'],
            'url': file_url(src, path)}


# ---- THE LOCAL SOURCES (architect 2026-10-07, the Clearlooks vocabulary): files read out of a disc image already on
# the build host, never fetched (fetch.py reads SOURCES alone). Each is pinned by its own sha256 (build.py checks it:
# a different byte is a hard fail) and by the image it came from. The Debian 6.0.10 squeeze live image is the ruling
# system the architect captured (tmp/squeeze/); its filesystem.squashfs is extracted with
#   unsquashfs -d tmp/squeeze_fs/fs -f tmp/squeeze_fs/live/filesystem.squashfs <path>
# (tmp/squeeze_fs/README.md), the files read as data, nothing executed. The engine's and metacity's source tarballs
# are the toolkit rule's citations (toolkit_rules.py's GTK 2 block), read, never parsed, like motif_rules above.
# The Windows 95 OSR2 disc (architect 2026-10-09) is the other local image: its Appearance schemes are registry data in
# shell2.inf, packed in the cabinet win95/PRECOPY1.CAB (a spanned set with PRECOPY2.CAB), extracted with
#   7z x tmp/W95_PLUS_AR.iso win95/PRECOPY1.CAB win95/PRECOPY2.CAB -o<scratch>
#   7z x <scratch>/win95/PRECOPY1.CAB shell2.inf -otmp/theme_sources/win95_shell2inf
# (7z needs both cabinets present to open the spanned set), the file read as data, nothing executed.
SQUEEZE_FS = os.path.join(REPO, 'tmp', 'squeeze_fs', 'fs')
WIN95_SHELL2INF_DIR = os.path.join(SOURCES_DIR, 'win95_shell2inf')
LOCAL_SOURCES = {
    'squeeze_live': dict(project='Debian 6.0.10 squeeze live (i386, GNOME desktop), the installed system read as data',
                         image='debian-live-6.0.10-i386-gnome-desktop.iso',
                         image_sha256='1cc527c3f9d51f6a25b04b56ce9eb1ae1dc967ec5d1468e19030edb34ff32e79',
                         url='https://cdimage.debian.org/mirror/cdimage/archive/6.0.10-live/i386/iso-hybrid/'
                             'debian-live-6.0.10-i386-gnome-desktop.iso',
                         filesystem='live/filesystem.squashfs',
                         filesystem_sha256='9cc5a7f216ed32d7f72e25651f814833964f49cbe928ffc750c7647858dbb24b',
                         files={'usr/share/themes/Clearlooks/gtk-2.0/gtkrc':
                                    ('gtk2-engines 1:2.20.1-1',
                                     '777f94f58b24f32cbf8d0e9edb176cf171b216e79a3a73fbdbbb13287ddacc6b'),
                                'usr/share/themes/Clearlooks/metacity-1/metacity-theme-1.xml':
                                    ('gnome-themes 2.30.2-1',
                                     '17293715eb7a6b45e1fd3e9554b9497b819deb5f555acb2cf3e78cfc30ca1c4a')}),
    'win95_shell2inf': dict(project='Windows 95 OSR2 (Microsoft), shell2.inf: the Appearance schemes of the shell '
                            'setup, registry data read as data from the retail disc image',
                            image='W95_PLUS_AR.iso',
                            image_sha256='53e6a0a96e7e0686c1bf718225d1c21e393db12158ebf475f7fc4e4c5ab6e8d9',
                            cabinet='win95/PRECOPY1.CAB',
                            cabinet_sha256='ec8a17fcf9e7bc016da3b7b90dd32058b8936d04985fb2128662a80fc0932850',
                            cabinet_next='win95/PRECOPY2.CAB',
                            cabinet_next_sha256='bac1a80d253c87c8d9d3da14156280919caa319acf2d621e94acbe70c190e433',
                            files={'shell2.inf': ('Windows 95 OSR2 shell2.inf, dated 1996-08-24',
                                                  '3bd8069637f1bd5ab1ab13b27c9300446ed4ec4b240ddbfe044345e64535a8b4')}),
    'gtk2_rules': dict(project='gtk-engines 2.20.2 (GNOME), the Clearlooks engine and its support library',
                       tarball='gtk-engines-2.20.2.tar.bz2',
                       tarball_sha256='15b680abca6c773ecb85253521fa100dd3b8549befeecc7595b10209d62d66b5',
                       url='https://download.gnome.org/sources/gtk-engines/2.20/gtk-engines-2.20.2.tar.bz2',
                       files=['engines/support/cairo-support.c', 'engines/clearlooks/src/clearlooks_draw.c',
                              'engines/clearlooks/src/clearlooks_draw_gummy.c',
                              'engines/clearlooks/src/clearlooks_style.c']),
    'metacity_rules': dict(project='metacity 2.30.3 (GNOME), the window manager\'s theme parser',
                           tarball='metacity-2.30.3.tar.bz2',
                           tarball_sha256='08f887018fa5e447cf184d03bae3fe2c05fdb7583bed6768e3b4d66392fc18dd',
                           url='https://download.gnome.org/sources/metacity/2.30/metacity-2.30.3.tar.bz2',
                           files=['src/ui/theme.c']),
}


def local_file(src, path):
    """Where a local source's file is on the build host (squeeze_live's and win95_shell2inf's alone are read)."""
    if src == 'win95_shell2inf': return os.path.join(WIN95_SHELL2INF_DIR, path)
    assert src == 'squeeze_live', src
    return os.path.join(SQUEEZE_FS, path)


def local_provenance(src, path):
    """The provenance record of one local source's file: the image (or tarball) it is read out of, with its sha256
    and URL, and for an image's file its package and its own sha256 (a squashfs image's file: the squashfs too; a
    cabinet image's file: the cabinet, its spanned continuation and their sha256s, the image having no URL)."""
    s = LOCAL_SOURCES[src]
    if 'cabinet' in s:
        package, sha = s['files'][path]
        return {'project': s['project'], 'file': path, 'package': package, 'sha256': sha, 'image': s['image'],
                'image_sha256': s['image_sha256'], 'cabinet': s['cabinet'], 'cabinet_sha256': s['cabinet_sha256'],
                'cabinet_next': s['cabinet_next'], 'cabinet_next_sha256': s['cabinet_next_sha256']}
    if 'image' in s:
        package, sha = s['files'][path]
        return {'project': s['project'], 'file': path, 'package': package, 'sha256': sha, 'image': s['image'],
                'image_sha256': s['image_sha256'], 'filesystem': s['filesystem'],
                'filesystem_sha256': s['filesystem_sha256'], 'url': s['url']}
    assert path in s['files'], (src, path)
    return {'project': s['project'], 'file': path, 'tarball': s['tarball'], 'tarball_sha256': s['tarball_sha256'],
            'url': s['url']}
