#pragma once

#include <cstddef>
#include <expected>
#include <filesystem>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>

// THE PROJECT'S SIDECAR SET AND ITS REQUIRED-FILE PREFLIGHT — the parser-domain
// core BOTH PRODUCTS COMPILE (architect approval 2026-09-16, the frozen-touch
// grant of 2026-09-15). The list and the all-or-nothing rule lived GUI-side
// until this file, where only the GUI asked them; the CLI and the render
// player's load-in-place read the files one after another instead, so a
// PARTIAL set was refused by whichever strict reader happened to run first,
// wearing that reader's cannot-open words, rather than by the set rule ahead of
// any parsing. Every road now asks THIS, then reads.
//
// Nothing here parses, opens or writes: it is a stat of the set's three names
// and, on the project roads, the retired ones (below). The GUI's
// GuiFailure-composing wrapper is sidecar_set_presence (settings_io.h) and is
// the only thing that knows about cards.

// THE ONE LIST: the three files a source carries beside it,
// `<stem><extension>`, as the product writes them (the set is three since the
// magnification level markers column's deletion, architect approval
// 2026-09-23). The project model's source rule (resolve_project), the
// required-file rule below, and the GitHub recheck's per-commit sidecar match,
// pathspecs and checkpoint paths (history_diff.cpp) all read it, and its ORDER
// is the order the recheck indexes its per-sidecar arrays by (warp markers,
// phase reset markers, settings) — and the order the refusal below names its
// first missing member in.
inline constexpr const char* kSidecarExtensions[] = {
    ".warpmarkers", ".phaseresetmarkers", ".settings",
};
inline constexpr std::size_t kSidecarCount = std::size(kSidecarExtensions);

// THE RETIRED SIDECARS (architect approval 2026-09-23): names that were once a
// member of the set and are no longer written or read. A project folder where
// one of them EXISTS beside the stem is REFUSED by the preflight below on the
// project roads (the Retired defect) rather than silently ignored — we never
// support legacy, and a file the product no longer reads sitting beside the
// source is a state the user deletes by hand. The roads that read an OLD
// SNAPSHOT of a project (the GitHub recheck's commits and exported members,
// the render-entry batch cells) ask the core with RetiredSidecars::Ignore and
// simply leave the retired file unread: it was that snapshot's own content,
// and the snapshot's three members still say everything the product reads.
inline constexpr const char* kRetiredSidecarExtensions[] = {
    ".magnificationlevelmarkers",
};

// WHICH SIDECAR, NAMED ONCE (architect approval 2026-09-16): the three indices
// into the list above are the ONE SPELLING OF "WHICH SIDECAR" everywhere. A
// caller names the member and this header names the file, so the extension
// strings live in this header alone — the composer below, the recheck's
// per-sidecar arrays and every load, save and batch-cell path all go through
// an index. The static_asserts pin each name to its string, so reordering the
// list without reordering these fails the build rather than silently swapping
// two columns' files.
inline constexpr std::size_t kSidecarWarp       = 0;
inline constexpr std::size_t kSidecarPhaseReset = 1;
inline constexpr std::size_t kSidecarSettings   = 2;

static_assert(kSidecarCount == 3);
static_assert(std::string_view(kSidecarExtensions[kSidecarWarp]) ==
              ".warpmarkers");
static_assert(std::string_view(kSidecarExtensions[kSidecarPhaseReset]) ==
              ".phaseresetmarkers");
static_assert(std::string_view(kSidecarExtensions[kSidecarSettings]) ==
              ".settings");

// The set's i-th path beside `parent`/`stem`, `i` named by the constants
// above. THE ONE COMPOSITION, so no road spells a sidecar name by hand
// (architect approval 2026-09-16).
inline std::filesystem::path sidecar_path(const std::filesystem::path& parent,
                                          const std::string&           stem,
                                          std::size_t                  i) {
    return parent / (stem + kSidecarExtensions[i]);
}

// SIDECAR PRESENCE IS ONE PREDICATE (the ONE owner). "Present" is EXISTS — not
// "is a regular file": a load skips its template creation for anything standing
// at a sidecar's name and then hands that name to the strict reader, so a
// directory or a socket wearing `<stem>.settings` is a PARSE FAILURE and not an
// absence, and the answer has to be the same on every road that asks or a
// preflight would approve a load the reader then refuses. A stat that FAILS is
// neither present nor absent: it answers with THE SYSTEM'S OWN WORDS, PATH-FREE
// (the caller owns the spelling of the path — full on a terminal line, the
// folder-and-file way on a card).
inline std::expected<bool, std::string> sidecar_exists(
        const std::filesystem::path& p) {
    std::error_code ec;
    const bool here = std::filesystem::exists(p, ec);
    if (ec) return std::unexpected(ec.message());
    return here;
}

// THE REQUIRED-FILE RULE, ONE OWNER (architect 2026-09-15: "we never support
// legacy — strictly migrate to the new and require manual update"): a source's
// sidecar set is ALL OR NOTHING. `None` — no sidecar present at all — and `All`
// are the two verdicts; SOME AND NOT ALL is the Missing defect below, naming
// the first absent file in kSidecarExtensions order.
//
// WHAT `None` MEANS IS THE CALLER'S, and the three callers differ by what they
// are allowed to author: the GUI's source load treats it as a NEW PROJECT and
// writes the three templates; the CLI and the render-entry load in place author
// nothing at all, so for them None is simply "all three missing" and refuses.
enum class SidecarSetPresence { None, All };

struct SidecarSetDefect {
    enum class Kind {
        Missing,     // `path` exists nowhere and at least one sibling does
        Unreadable,  // the stat on `path` itself failed; `reason` is its words
        Retired,     // `path` is a retired sidecar that exists (architect
                     // approval 2026-09-23); every road words it as
                     // "'<file>' is no longer part of the sidecar set; delete
                     // it"
    };
    Kind                  kind = Kind::Missing;
    std::filesystem::path path;
    std::string           reason;  // Unreadable only; path-free by contract
};

// WHETHER THE PREFLIGHT ASKS THE RETIRED NAMES (architect approval
// 2026-09-23), spelled at every call site. REFUSE on the roads that open the
// LIVE project — the GUI's load and its dry run (the picker, Revert), and the
// CLI — where a retired file is load-fatal. IGNORE on the render-entry load in
// place, whose batch cell is an old snapshot the product wrote and whose
// retired copy is simply left unread (kRetiredSidecarExtensions above).
enum class RetiredSidecars { Refuse, Ignore };

// The preflight: stat the three names (and, under Refuse, the retired ones,
// FIRST) and answer before anything is parsed. Pure but for the stats; no file
// is opened, read or created. A retired name is asked ahead of the set so a
// folder holding one is refused as that file whatever else it holds — a new
// project's folder (no member of the set) included, which the model calls a
// new project (resolve_project) and this refuses before any template is
// written.
inline std::expected<SidecarSetPresence, SidecarSetDefect>
sidecar_set_presence_core(const std::filesystem::path& parent,
                          const std::string&           stem,
                          RetiredSidecars              retired) {
    if (retired == RetiredSidecars::Refuse) {
        for (const char* ext : kRetiredSidecarExtensions) {
            const std::filesystem::path p = parent / (stem + ext);
            auto here = sidecar_exists(p);
            if (!here) {
                return std::unexpected(SidecarSetDefect{
                    SidecarSetDefect::Kind::Unreadable, p,
                    std::move(here.error())});
            }
            if (*here) {
                return std::unexpected(SidecarSetDefect{
                    SidecarSetDefect::Kind::Retired, p, {}});
            }
        }
    }
    std::size_t                          present = 0;
    std::optional<std::filesystem::path> first_missing;
    for (std::size_t i = 0; i < kSidecarCount; ++i) {
        const std::filesystem::path p = sidecar_path(parent, stem, i);
        auto here = sidecar_exists(p);
        if (!here) {
            return std::unexpected(SidecarSetDefect{
                SidecarSetDefect::Kind::Unreadable, p,
                std::move(here.error())});
        }
        if (*here) {
            ++present;
        } else if (!first_missing) {
            first_missing = p;
        }
    }
    if (present == 0)             return SidecarSetPresence::None;
    if (present == kSidecarCount) return SidecarSetPresence::All;
    return std::unexpected(SidecarSetDefect{SidecarSetDefect::Kind::Missing,
                                            *first_missing, {}});
}

// THE GIT-TREE DOMAIN ASKS THE SAME QUESTION AND CANNOT ASK IT HERE: the
// GitHub recheck's whole-set gate (load_commit_sidecars_strict,
// history_diff.h) requires the same three members of a COMMIT's tree, where
// "present" is a blob listed at the path rather than a name on this disk, and
// it refuses a partial set for the identical reason (a load-in-place is a
// whole-state replace, and inheriting some members from the checkpoint and the
// rest from nowhere composes a state no checkpoint ever was). Two domains, one
// rule, stated in both places on purpose — there is no filesystem to stat in
// that one.
