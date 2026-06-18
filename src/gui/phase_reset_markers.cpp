#include "phase_reset_markers.h"

#include "time_format.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <limits>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

std::expected<void, std::string> GuiPhaseResetMarkers::load(const std::string& path) {
    markers_.clear();
    ++generation_;

    auto r = parse_phaseresetmarkers_file(path);
    if (!r) return std::unexpected(std::move(r.error()));

    markers_.reserve(r->size());
    for (const PhaseResetMarker& pm : *r) {
        GuiPhaseResetMarker g;                   // no extra fields today
        static_cast<PhaseResetMarker&>(g) = pm;  // copy the serialized base
        markers_.push_back(g);
    }
    return {};
}

bool GuiPhaseResetMarkers::save(const std::string& path) const {
    return save(path, markers_);
}

bool GuiPhaseResetMarkers::save(const std::string& path,
                         const std::vector<GuiPhaseResetMarker>& markers_) {
    // Mid-edit nudge gestures may transit through equal-time collisions.
    // Drop duplicates silently here (keep the first occurrence) and emit
    // a one-line stderr notice so the user sees that the on-disk content
    // diverges from the in-memory list. Dedup is keyed on time_seconds
    // (exact double match, matching warp-marker save behavior).
    std::vector<GuiPhaseResetMarker> deduped;
    deduped.reserve(markers_.size());
    double last_time = std::numeric_limits<double>::lowest();
    int dropped = 0;
    for (const auto& m : markers_) {
        const double eff = m.time_seconds;
        if (eff == last_time) {
            ++dropped;
            continue;
        }
        deduped.push_back(m);
        last_time = eff;
    }
    if (dropped > 0) {
        std::fprintf(stderr,
            "warptempo_gui: dropped %d duplicate phase_reset(s) on save\n",
            dropped);
    }

    std::ostringstream out;
    for (const auto& m : deduped) {
        // `[#]MM:SS.mmm` only. The `#` disable prefix composes ahead of the
        // timestamp, exactly as the parser strips it. No mode suffix — the
        // peak/heap/pass model was removed when heap became the sole engine.
        if (m.disabled) out << '#';
        out << format_timestamp(m.time_seconds) << '\n';
    }
    const std::string data = out.str();

    mode_t mode = 0644;
    struct stat st;
    if (::stat(path.c_str(), &st) == 0) {
        mode = st.st_mode & 07777;
    }

    const std::string tmp_path = path + ".tmp";
    int fd = ::open(tmp_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, mode);
    if (fd < 0) return false;

    size_t written = 0;
    while (written < data.size()) {
        const ssize_t n = ::write(fd, data.data() + written,
                                  data.size() - written);
        if (n < 0) {
            if (errno == EINTR) continue;
            ::close(fd);
            ::unlink(tmp_path.c_str());
            return false;
        }
        written += static_cast<size_t>(n);
    }
    if (::fsync(fd) != 0) {
        ::close(fd);
        ::unlink(tmp_path.c_str());
        return false;
    }
    if (::close(fd) != 0) {
        ::unlink(tmp_path.c_str());
        return false;
    }
    ::chmod(tmp_path.c_str(), mode);

    if (::rename(tmp_path.c_str(), path.c_str()) != 0) {
        ::unlink(tmp_path.c_str());
        return false;
    }
    return true;
}

bool GuiPhaseResetMarkers::delete_file(const std::string& path) const {
    if (path.empty()) return false;
    if (::unlink(path.c_str()) == 0) return true;
    if (errno == ENOENT) return true;
    return false;
}

int GuiPhaseResetMarkers::insert_marker(GuiPhaseResetMarker m) {
    const double time = m.time_seconds;
    auto it = std::lower_bound(
        markers_.begin(), markers_.end(), time,
        [](const GuiPhaseResetMarker& a, double t) { return a.time_seconds < t; });
    const int idx = static_cast<int>(it - markers_.begin());
    markers_.insert(it, std::move(m));
    ++generation_;
    return idx;
}

void GuiPhaseResetMarkers::remove_marker(int index) {
    if (index < 0 || index >= static_cast<int>(markers_.size())) return;
    markers_.erase(markers_.begin() + index);
    ++generation_;
}
