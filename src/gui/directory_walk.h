#pragma once

#include <filesystem>
#include <system_error>
#include <utility>

// THE NON-THROWING DIRECTORY WALK — the GUI's one owner for "list a folder
// without letting the filesystem terminate the process".
//
// WHY IT EXISTS. std::filesystem's default overloads throw
// std::filesystem::filesystem_error, and a range-for over a directory_iterator
// throws in THREE places, only one of which is obvious: the construction (which
// the error_code overload silences), the INCREMENT the loop performs after
// every entry (which has no error_code form inside a range-for at all), and
// every query asked of the entry — is_regular_file(), is_directory(),
// file_size() — whose bare overloads throw on a stat failure. Handing the
// constructor an error_code therefore buys nothing on its own: a folder removed,
// unmounted or made unreadable WHILE a listing is being built still throws out
// of the loop, and the GUI has no handler anywhere above it, so the process
// dies. The events are ordinary here, not adversarial: `l` prunes and lists
// `render/` and `tmp/` on a project the sync script or the trash road is
// editing under it, a render dispatch scans the batch root, an OTG stick is
// pulled mid-listing.
//
// THE CONTRACT. Construct with `ec`; walk to the end; increment with `ec` and
// stop at the FIRST error; hand every entry to `fn` in between. `ec` is the
// caller's own — cleared before the walk, and left holding the fault when one
// happened, so a caller that cares can say so and a caller that does not simply
// answers WHAT IT SAW (the empty or partial listing its normal empty/refusal
// behaviour already handles). `fn` takes a const directory_entry& and asks it
// with the ERROR_CODE overloads: this owner cannot ask for the caller, and an
// entry query that throws would defeat the whole point.
//
// EVERY WALK IN src/gui, and which ones spell their own increment instead
// (re-grep `directory_iterator` when this list is retold):
//
//   THROUGH THIS OWNER
//     renders_dir.cpp          enumerate_render_entries — the batch roots and,
//                              per batch, its cells (two walks); and
//                              prune_render_folder — the `render/` folder,
//                              classified whole and then removed from, so no
//                              iterator is live while the folder changes
//     input_render_dispatch.cpp  max_renders_batch_index — the batch root's
//                              numbering; and the miscellaneous cell's own
//                              next-index scan (two walks)
//     project_model.cpp        enumerate_project_names — the projects list
//     render_cache.cpp         RenderCache::sweep_orphans — the stale PID dirs
//
//   THEIR OWN, EACH FOR A REASON
//     external_sync.cpp        walk_directory — the same shape plus TWO things
//                              this owner does not have: a callback that
//                              REPORTS A FAULT STRING and stops the walk at
//                              once (the mirror's rule 1), and an
//                              `optional_root` ENOENT carve-out on the
//                              directory itself. It is the sibling, not a
//                              duplicate: the mirror's faults are sentences,
//                              not error codes.
//     project_model.cpp        resolve_project — refuses MID-WALK with the
//                              offending entry's own sentence ("Cannot read
//                              '<name>' in '<folder>'"), which a void callback
//                              cannot return.
//
// (A THIRD STOOD HERE UNTIL 2026-08-30: platform_wayland.cpp's
// removable_volume, whose per-entry symlink or status fault refused the whole
// question with its own sentence. It went with the discovery itself — the
// mirror's destination is the device config's `sync_path` now, told and not
// found — and no backend walks a directory any more.)
//
// This header is GUI-side and header-only on purpose: the frozen directories
// walk no directories, and there is nothing here to link.
template <typename Fn>
void for_each_directory_entry(const std::filesystem::path& dir,
                              std::error_code&             ec,
                              Fn&&                         fn) {
    ec.clear();
    std::filesystem::directory_iterator it(dir, ec);
    if (ec) return;
    const std::filesystem::directory_iterator end;
    while (it != end) {
        fn(*it);
        it.increment(ec);
        if (ec) return;
    }
}
