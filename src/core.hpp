#pragma once
#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace mb {
struct Chord {
    int key = 0;
    uint8_t modifiers = 0;
    bool operator==(Chord const&) const = default;
};
struct Binding { std::string path; Chord chord; };

class Bindings {
    std::vector<Binding> m_items;
public:
    auto const& items() const { return m_items; }
    void clear(std::string const& path) {
        std::erase_if(m_items, [&](auto const& item) { return item.path == path; });
    }
    // One action per chord; rebinding moves the key instead of loading two macros.
    void assign(std::string path, Chord chord) {
        if (path.empty() || chord.key <= 0) return;
        std::erase_if(m_items, [&](auto const& item) {
            return item.path == path || item.chord == chord;
        });
        m_items.push_back({std::move(path), chord});
    }
    std::optional<Chord> keyFor(std::string const& path) const {
        for (auto const& item : m_items) if (item.path == path) return item.chord;
        return {};
    }
    std::optional<std::string> pathFor(Chord chord) const {
        for (auto const& item : m_items) if (item.chord == chord) return item.path;
        return {};
    }
};

// A consumed press owns its repeat AND release, even if modifiers, text focus,
// menu visibility or bindings changed while the key was held.
class PressGate {
    std::unordered_set<int> m_consumed;
public:
    bool consumed(int key) const { return m_consumed.contains(key); }
    void press(int key) { m_consumed.insert(key); }
    bool release(int key) { return m_consumed.erase(key) != 0; }
    void reset() { m_consumed.clear(); }
};

struct LoadContext {
    bool active = false;
    bool succeeded = false;
    void* notification = nullptr;
    void begin() { active = true; succeeded = false; notification = nullptr; }
    void end() { active = false; notification = nullptr; }
    void created(void* value, std::string_view text) {
        if (active && value && (text == "Macro Loaded" || text == "Macros Loaded")) {
            succeeded = true;
            notification = value;
        }
    }
    bool mute(void* value) {
        if (!active || !value || value != notification) return false;
        notification = nullptr;
        return true;
    }
};

inline std::string lowerAscii(std::string text) {
    for (char& c : text) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return text;
}
inline bool macroExtension(std::string extension) {
    extension = lowerAscii(std::move(extension));
    return extension == ".gdr" || extension == ".gdr2" || extension == ".json" ||
           extension == ".xd" || extension == ".slc" || extension == ".cml";
}
}
