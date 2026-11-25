// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <anira/models/core/Segment.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace anira::examples {

/// What a speech example shows (TextLike, transcript()): its last lines, oldest first.
struct Transcript {
    struct Line {
        std::string m_text;
        double m_seconds = 0.0;      ///< where it starts, seconds since the stream began
        double m_end_seconds = 0.0;  ///< where it ends
        int m_speaker = -1;          ///< -1: no speaker
        bool m_final = true;         ///< false: still revised
    };
    std::vector<Line> m_lines;
};

/// An example with text (an analyzer of speech): transcript(out) on the UI thread fills `out`.
template <class E>
concept TextLike = requires(const E& e, Transcript& out) { e.transcript(out); };

/// The example's lines into `out` (none for an example without text).
template <class E>
void transcript_of(const E& example, Transcript& out) {
    if constexpr (TextLike<E>) {
        example.transcript(out);
    } else {
        out.m_lines.clear();
    }
}

/// The lines a speech example keeps: a segment's revisions replace its line (the same id), the
/// oldest goes beyond `capacity`. Any thread (a mutex: not the audio thread's).
class TranscriptBook {
public:
    explicit TranscriptBook(std::size_t capacity = 64) : m_capacity(capacity) {}

    /// A segment, positions in samples at `sample_rate` (one without text is left out).
    void add(const models::core::Segment& segment, double sample_rate) {
        if (segment.m_text.find_first_not_of(" \t\n") == std::string::npos) { return; }
        const std::scoped_lock lock(m_mutex);
        Transcript::Line line{
            .m_text = segment.m_text,
            .m_seconds = static_cast<double>(segment.m_start_sample) / sample_rate,
            .m_end_seconds = static_cast<double>(segment.m_end_sample) / sample_rate,
            .m_speaker = segment.m_speaker,
            .m_final = segment.m_is_final};
        const auto it = std::ranges::find(m_ids, segment.m_id);
        if (it != m_ids.end()) {
            m_lines[static_cast<std::size_t>(it - m_ids.begin())] = std::move(line);
            return;
        }
        m_ids.push_back(segment.m_id);
        m_lines.push_back(std::move(line));
        if (m_lines.size() > m_capacity) {
            m_ids.pop_front();
            m_lines.pop_front();
        }
    }
    void clear() {
        const std::scoped_lock lock(m_mutex);
        m_ids.clear();
        m_lines.clear();
    }
    void fill(Transcript& out) const {
        const std::scoped_lock lock(m_mutex);
        out.m_lines.assign(m_lines.begin(), m_lines.end());
    }

private:
    std::size_t m_capacity;
    mutable std::mutex m_mutex;
    std::deque<std::uint64_t> m_ids;
    std::deque<Transcript::Line> m_lines;
};

}  // namespace anira::examples
