#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

// A FAILURE THAT REACHES A CARD IS TWO CLAUSES, COMPOSED AT ITS ONE FAILURE
// POINT (architect 2026-09-02 — "option 1, applied universally"). The
// terminal and the card want different sentences about one fact: the
// terminal wants the FULL PATH and every word (it is the
// debugging surface, and on the tablet it is logcat), while the card is one
// line of a small stack that CLIPS, so a path there names the FILE the
// basename rule's way (below) and the system's own words follow it.
// Until this landed the two surfaces shared ONE composed string — chosen so
// they could not drift — and the render road's card ended mid-path on the
// architect's ~160-character titles, the system's words never reaching the
// screen.
//
// THE SHAPE KEEPS THE ONE-COMPOSER GUARANTEE: both clauses are built at the
// site that has the path and the error, from that structured data, and NEVER
// by parsing the composed English of one clause into the other — a path with
// a quote or a colon in it would defeat any such reduction. Where a failure crosses a thread (the render
// worker's completion, the history scan's result) this STRUCT rides the completion, so the GUI thread chooses which
// clause it raises and the worker prints the other.
//
//   diagnostic — the stderr line's clause: full paths, every word, the
//                instruction sentence a stderr reader can act on.
//   display    — the card's clause: ONE clause under the card rules
//                (notifications.h's "words on a card"), the path named the way
//                its family names it, the system's words as they arrive.
//
// THE BASENAME RULE (this header owns it): a message that carries a
// filesystem path names the FILE — its basename, a batch cell's
// `N_tag/NN.wav` id, or the project folder and file — never the full path,
// the subject being the row on screen and the card's clip the backstop. ONE
// SENTENCE PER REASON AND NO ROAD FORK: the fatal roads that print the same
// words (startup's resolve, gui_main's reopen) print the shortened sentence
// too, the projects path being the device config's one setting. A frozen
// loader that bundles its path into its sentence publishes THE WORDS WITH NO
// PATH beside it (path_free_reason, an optional out-parameter its open and
// read arms alone write; its presence is the discriminator), so an appending
// composer names the file once, its family's way, and never parses the
// path back out of the English.
//
// EACH FAMILY KEEPS ITS OWN NAMING RULE and hands the shown spelling in: the
// project's files through shown_project_path (device_config.h — the folder
// and the file, `render/x.wav`, `3_bpm/01.settings`), the open
// project's own sidecars by bare basename (save_ops.cpp), a batch folder by
// its name (renders_dir.h). The three composers below only ASSEMBLE the two
// clauses from the parts; they decide no name. `plain_failure` is the
// no-path case — the same words on both surfaces, which is what every
// sentence without a path already was.
struct GuiFailure {
    std::string diagnostic;
    std::string display;
};

inline GuiFailure plain_failure(std::string words) {
    GuiFailure f;
    f.diagnostic = words;
    f.display    = std::move(words);
    return f;
}

// "<before>'<path>'<after>" on both surfaces — the full path on the
// diagnostic, `shown` on the display. `after` carries the system's words
// where there are any (": " + ec.message()), and is empty otherwise.
inline GuiFailure path_failure(std::string_view             before,
                               const std::filesystem::path& full,
                               std::string_view             shown,
                               std::string_view             after) {
    GuiFailure f;
    f.diagnostic.reserve(before.size() + full.native().size() +
                         after.size() + 2);
    f.diagnostic.append(before).append("'").append(full.string())
        .append("'").append(after);
    f.display.reserve(before.size() + shown.size() + after.size() + 2);
    f.display.append(before).append("'").append(shown).append("'")
        .append(after);
    return f;
}

// "<before>'<a>'<between>'<b>'<after>" — the two-path sentence (a rename's
// staging and final names, the dry run's settings file and the output it
// would make collide with the source).
inline GuiFailure two_path_failure(std::string_view             before,
                                   const std::filesystem::path& full_a,
                                   std::string_view             shown_a,
                                   std::string_view             between,
                                   const std::filesystem::path& full_b,
                                   std::string_view             shown_b,
                                   std::string_view             after) {
    GuiFailure f;
    f.diagnostic.append(before).append("'").append(full_a.string())
        .append("'").append(between).append("'").append(full_b.string())
        .append("'").append(after);
    f.display.append(before).append("'").append(shown_a).append("'")
        .append(between).append("'").append(shown_b).append("'")
        .append(after);
    return f;
}
