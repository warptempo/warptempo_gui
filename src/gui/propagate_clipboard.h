#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// THE PROPAGATE FAMILY'S CLIPBOARD SHAPE, shared by its two members (the phase
// reset propagate, phase_reset_clipboard.h, and the magnification level
// propagate, magnification_level_clipboard.h — 2026-09-15): a session-only,
// single-slot, in-memory clipboard holding a SEQUENCE OF NAMED WARP BLOCKS,
// each carrying the placements a copy captured inside it. Never persisted to
// any sidecar, cleared on app exit.
//
// WHAT IS SHARED IS THE BLOCK AND THE SLOT, NOT THE PLACEMENT: a block is the
// same thing to both families — the owning warp marker's label, its section's
// extent in absolute source frames (what the state paste re-buckets against),
// and the placements captured in it — while what a placement IS differs per
// column (an anchored phase reset with its disabled bit; a magnification level
// marker's own frame with its level and its disabled bit), so the placement is
// the template parameter and each family's header states its own.
//
// EACH FAMILY HAS ITS OWN SLOT (architect 2026-09-15): a copy of one column
// never overwrites the other's clipboard, so the two slots are two members of
// AppState, each an instantiation of the container below.

template <class Placement>
struct PropagateClipboardBlock {
    std::string            label_name;
    int64_t                source_start_frame = 0;  // absolute source frames
    int64_t                source_end_frame   = 0;  // absolute source frames
    std::vector<Placement> placements;
};

template <class Block>
class PropagateClipboard {
public:
    void set(std::vector<Block> blocks)          { blocks_ = std::move(blocks); }
    void clear()                                 { blocks_.clear(); }
    bool empty() const                           { return blocks_.empty(); }
    const std::vector<Block>& blocks() const     { return blocks_; }

private:
    std::vector<Block> blocks_;
};
