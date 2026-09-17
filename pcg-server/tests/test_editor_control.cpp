// Standalone regression: c++ -std=c++17 -pthread -Isrc tests/test_editor_control.cpp -o /tmp/test-editor-control
#include "editor_control.hpp"

#include <cassert>
#include <iostream>
#include <thread>

using pcg_server::EditorControlAllowed;
using pcg_server::ScopedEditorControl;

int main() {
    assert(!EditorControlAllowed("", true, true, 1));
    assert(!EditorControlAllowed("chosen", false, true, 1));
    assert(!EditorControlAllowed("chosen", true, false, 1));
    assert(!EditorControlAllowed("chosen", true, true, 0));
    assert(EditorControlAllowed("chosen", true, true, 1));
    {
        const ScopedEditorControl outer("chosen", 1);
        assert(EditorControlAllowed("chosen", true, true, 1));
        assert(!EditorControlAllowed("other", true, true, 1));
        assert(!EditorControlAllowed("chosen", true, true, 3)); // revoke/regrant
        {
            const ScopedEditorControl inner("other", 2);
            assert(EditorControlAllowed("other", true, true, 2));
            assert(!EditorControlAllowed("chosen", true, true, 1));
        }
        assert(EditorControlAllowed("chosen", true, true, 1));
        std::thread independent([] {
            const ScopedEditorControl scope("other", 3);
            assert(EditorControlAllowed("other", true, true, 3));
            assert(!EditorControlAllowed("chosen", true, true, 1));
        });
        independent.join();
        assert(EditorControlAllowed("chosen", true, true, 1));
    }
    assert(EditorControlAllowed("other", true, true, 3));
    std::cout << "editor control policy: 15 assertions passed\n";
}
