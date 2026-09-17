#pragma once

#include <cstdint>
#include <string>

namespace pcg_server {

// Pin the page's consent generation for one synchronous tool invocation. A
// long-running bake cannot enqueue work under a new grant after revoke/regrant.
// Thread-local scope is intentional: MCP requests run on separate HTTP workers.
class ScopedEditorControl {
public:
    ScopedEditorControl(const std::string& session_id, int64_t revision)
        : session_id_(session_id), revision_(revision), previous_(current_) {
        current_ = this;
    }
    ~ScopedEditorControl() { current_ = previous_; }
    ScopedEditorControl(const ScopedEditorControl&) = delete;
    ScopedEditorControl& operator=(const ScopedEditorControl&) = delete;

    static bool Matches(const std::string& session_id, int64_t revision) {
        return current_ == nullptr ||
               (current_->session_id_ == session_id && current_->revision_ == revision);
    }

private:
    std::string session_id_;
    int64_t revision_;
    const ScopedEditorControl* previous_;
    inline static thread_local const ScopedEditorControl* current_ = nullptr;
};

inline bool EditorControlAllowed(
    const std::string& requested_id, bool online, bool approved, int64_t revision) {
    return !requested_id.empty() && online && approved && revision > 0 &&
           ScopedEditorControl::Matches(requested_id, revision);
}

}  // namespace pcg_server
