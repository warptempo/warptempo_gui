#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// THE PHASE RESET PROPAGATE'S CLIPBOARD SHAPE (phase_reset_clipboard.h, its
// one instantiation): a session-only, single-slot, in-memory clipboard holding
// a SEQUENCE OF NAMED WARP BLOCKS, each carrying the placements a copy
// captured inside it — the owning warp marker's label, its section's extent in
// absolute source frames (what the state paste re-buckets against), and the
// placements. Never persisted to any sidecar, cleared on app exit.
//
// THE BLOCK AND THE SLOT ARE SPLIT FROM THE PLACEMENT because the placement is
// the part that carries a column's own meaning, and that split is why these
// are templates. They had a SECOND INSTANTIATION from 2026-09-15 to
// 2026-09-19, the magnification level propagate's clipboard, which bucketed by
// the same named warp blocks; that family became a PURE FRAME DISTANCE from
// the playhead and its clipboard is a flat list of offsets with no block in it
// at all (magnification_level_clipboard.h), so what is left here is the phase
// family's shape alone.

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
