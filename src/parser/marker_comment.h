#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

// THE MARKER COMMENT — one grammar, one split, one byte class (architect
// approval 2026-08-19, the frozen reopen this header was created under).
//
// Every warp and phase-reset marker line may carry a free-text comment,
// appended to the otherwise whitespace-free canonical line as:
//
//     <canonical line><space>//<comment>
//
// The canonical prefix keeps its byte-exact discipline (no space, tab, or CR
// anywhere in it); the comment that follows is free UTF-8 text and is the ONE
// place in a marker grammar where a space may appear. The separator is the
// FIRST occurrence of " //" on the line — the prefix cannot contain a space,
// so no earlier candidate exists, and everything past it belongs to the
// comment verbatim, a further " //" included.
//
// This header is header-only and shared by both binaries — the frame_format.h
// precedent — so the split and the byte class have exactly one spelling.
// The consumers are the two file parsers (warpmarkers_parse.cpp,
// phaseresetmarkers_parse.cpp), the GitHub recheck's two delta extractors
// (history_diff.cpp) and the revert arms' reconstitution; none mirrors the
// split.

// Maximum comment length in BYTES, shared by both binaries. A multi-byte
// codepoint spends the bytes it costs, so this is a storage bound rather than
// a character count — the same reading every text_editor cap takes.
inline constexpr size_t kMaxMarkerCommentBytes = 99;

// The result of splitting one raw marker line. `prefix` is the canonical
// line the position/payload parsers see; `comment` is the raw comment bytes
// (unvalidated — validate_marker_comment below is the judge); `had_comment`
// distinguishes "no separator on the line" from "separator with an empty
// tail", which is a distinction the load rules need: an EMPTY comment is
// load-fatal, because the writer never emits the bare ` //` suffix and the
// editor's empty commit REMOVES the comment instead of storing one.
//
// The views alias the caller's buffer; they are valid only as long as it is.
struct MarkerCommentSplit {
    std::string_view prefix;
    std::string_view comment;
    bool             had_comment = false;
};

inline MarkerCommentSplit split_marker_comment(std::string_view line) {
    MarkerCommentSplit out;
    const size_t sep = line.find(" //");
    if (sep == std::string_view::npos) {
        out.prefix = line;
        return out;
    }
    out.prefix      = line.substr(0, sep);
    out.comment     = line.substr(sep + 3);
    out.had_comment = true;
    return out;
}

// THE COMMENT BYTE CLASS, enforced identically at load (here) and at type
// time (text_editor::replace_selection's incoming filter, plus the comment
// editor's own byte cap): 1..kMaxMarkerCommentBytes bytes, well-formed UTF-8,
// no ASCII control byte (0x00..0x1f — \r and \t included, which is what keeps
// the CRLF corruption tripwire alive now that a line may carry free text) and
// no DEL. Every refusal here names a state the GUI can never produce, so each
// is adversarial and load-fatal, first error only — the two-category rule
// holds exactly: a comment that commits in the editor loads back.
//
// Returns true on success; on failure returns false and sets `error_out` to a
// one-line diagnostic in the readers' voice.
inline bool validate_marker_comment(std::string_view comment,
                                    std::string& error_out) {
    if (comment.empty()) {
        error_out = "empty comment after ' //'";
        return false;
    }
    if (comment.size() > kMaxMarkerCommentBytes) {
        error_out = "comment must be at most " +
                    std::to_string(kMaxMarkerCommentBytes) + " bytes";
        return false;
    }
    // Well-formed UTF-8, control-free. The sequence length comes from the lead
    // byte, every continuation byte is checked, and the decoded value is
    // range-checked so an OVERLONG form, a surrogate, and a value past
    // U+10FFFF are all refused — the same acceptance the editor's incoming
    // filter applies, stated once per side of the boundary.
    const size_t n = comment.size();
    size_t       i = 0;
    while (i < n) {
        const unsigned char b0 = static_cast<unsigned char>(comment[i]);
        if (b0 < 0x80) {
            if (b0 < 0x20 || b0 == 0x7f) {
                error_out = "control byte in comment";
                return false;
            }
            ++i;
            continue;
        }
        size_t   len = 0;
        uint32_t cp  = 0;
        if      ((b0 & 0xe0) == 0xc0) { len = 2; cp = b0 & 0x1fu; }
        else if ((b0 & 0xf0) == 0xe0) { len = 3; cp = b0 & 0x0fu; }
        else if ((b0 & 0xf8) == 0xf0) { len = 4; cp = b0 & 0x07u; }
        else {
            error_out = "malformed UTF-8 in comment";
            return false;
        }
        if (i + len > n) {
            error_out = "malformed UTF-8 in comment";
            return false;
        }
        for (size_t k = 1; k < len; ++k) {
            const unsigned char bk = static_cast<unsigned char>(comment[i + k]);
            if ((bk & 0xc0) != 0x80) {
                error_out = "malformed UTF-8 in comment";
                return false;
            }
            cp = (cp << 6) | (bk & 0x3fu);
        }
        static constexpr uint32_t kShortest[5] = {0, 0, 0x80, 0x800, 0x10000};
        if (cp < kShortest[len] || cp > 0x10ffffu ||
            (cp >= 0xd800u && cp <= 0xdfffu)) {
            error_out = "malformed UTF-8 in comment";
            return false;
        }
        i += len;
    }
    return true;
}
