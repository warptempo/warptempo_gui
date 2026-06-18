#include "warpmarkers.h"

#include "time_format.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

std::expected<void, std::string> GuiWarpMarkers::load(const std::string& path) {
    markers_.clear();
    ++generation_;

    auto r = parse_warpmarkers_file(path);
    if (!r) return std::unexpected(std::move(r.error()));

    markers_.reserve(r->size());
    for (const WarpMarker& wm : *r) {
        GuiWarpMarker g;                    // session-only fields default
        static_cast<WarpMarker&>(g) = wm;   // copy the serialized base
        markers_.push_back(g);
    }
    return {};
}

bool GuiWarpMarkers::save(const std::string& path) const {
    return save(path, markers_);
}

bool GuiWarpMarkers::save(const std::string& path,
                      const std::vector<GuiWarpMarker>& markers_) {
    std::ostringstream out;
    for (const auto& m : markers_) {
        // Canonical new format, no whitespace anywhere on the line:
        //   [#]?MM:SS.SSS|PAYLOAD
        // PAYLOAD per Part 1A of V.A1.
        if (m.disabled) out << '#';
        out << format_timestamp(m.time_seconds) << '|';

        // Defensive guard against invalid in-memory combinations.
        const bool both_def_and_ref =
            !m.label_def.empty() && !m.label_ref.empty();
        if (both_def_and_ref) {
            std::fprintf(stderr,
                "warptempo_gui: marker at %.3fs has both label_ref and "
                "label_def; emitting as reference\n",
                m.time_seconds);
        }

        // Payload:
        //   label_ref               → "a.42"
        //   inherit, no def         → "pass"
        //   inherit, with def       → "pass:a.42"
        //   owning, no scale        → "1.23"
        //   owning, with scale      → "1.23*1.2345"
        //   def, no scale           → "1.23:a.03"
        //   def, with scale         → "1.23*1.2345:a.03"
        if (!m.label_ref.empty()) {
            out << m.label_ref;
        } else {
            if (m.tempo_inherits) {
                out << "pass";
            } else {
                char tbuf[32];
                std::snprintf(tbuf, sizeof(tbuf), "%.2f", m.tempo_base);
                out << tbuf;
                if (!m.tempo_scale.empty()) {
                    out << '*' << warpmarkers_internal::normalize_scale_string(m.tempo_scale);
                }
            }
            if (!m.label_def.empty()) {
                out << ':' << m.label_def;
            }
        }

        out << '\n';
    }
    const std::string data = out.str();

    // Preserve existing mode or fall back to 0644.
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

int GuiWarpMarkers::insert_marker(GuiWarpMarker m) {
    auto it = std::lower_bound(
        markers_.begin(), markers_.end(), m.time_seconds,
        [](const GuiWarpMarker& a, double t) { return a.time_seconds < t; });
    const int idx = static_cast<int>(it - markers_.begin());
    markers_.insert(it, std::move(m));
    ++generation_;
    return idx;
}

void GuiWarpMarkers::remove_marker(int index) {
    if (index < 0 || index >= static_cast<int>(markers_.size())) return;
    markers_.erase(markers_.begin() + index);
    ++generation_;
}

bool effective_disabled(const std::vector<GuiWarpMarker>& markers, int idx) {
    if (idx < 0 || idx >= static_cast<int>(markers.size())) return false;
    const auto& m = markers[idx];
    if (m.disabled) return true;
    if (!m.label_ref.empty()) {
        // Walk all markers to find the definition. O(N^2) across the list
        // but N is small (hundreds max).
        for (const auto& other : markers) {
            if (!other.label_def.empty() &&
                other.label_def == m.label_ref) {
                return other.disabled;
            }
        }
    }
    return false;
}
