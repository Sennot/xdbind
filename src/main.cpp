#include "core.hpp"
#include "bridge.hpp"
#include <Geode/Geode.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>
#include <Geode/utils/Keyboard.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <filesystem>
#include <set>

using namespace geode::prelude;
namespace fs = std::filesystem;

namespace mb {
class BindsPopup;
BindsPopup* openPopup = nullptr;
Bindings bindings;
PressGate gate;
bool bindingsRead = false;

std::string utf8(fs::path const& path) {
    auto bytes = path.generic_u8string();
    return {bytes.begin(), bytes.end()};
}
fs::path fromUtf8(std::string const& text) { return fs::u8path(text); }

void readBindings() {
    if (bindingsRead) return;
    bindingsRead = true;
    auto json = Mod::get()->getSavedValue<matjson::Value>("bindings", matjson::Value::array());
    auto array = json.asArray();
    if (!array) return;
    for (auto const& row : array.unwrap()) {
        if (!row.isObject()) continue;
        auto path = row["path"].asString();
        auto key = row["key"].asInt();
        auto mods = row["modifiers"].asInt();
        if (!path || !key || !mods || key.unwrap() <= 0 || key.unwrap() > 255 ||
            mods.unwrap() < 0 || mods.unwrap() > 15) continue;
        bindings.assign(path.unwrap(), {static_cast<int>(key.unwrap()), static_cast<uint8_t>(mods.unwrap())});
    }
}
bool saveBindings() {
    auto json = matjson::Value::array();
    for (auto const& item : bindings.items()) {
        json.push(matjson::makeObject({{"path", item.path}, {"key", item.chord.key},
            {"modifiers", static_cast<int>(item.chord.modifiers)}}));
    }
    Mod::get()->setSavedValue("bindings", json);
    auto saved = Mod::get()->saveData();
    if (!saved) {
        log::error("Macro Binds save failed: {}", saved.unwrapErr());
        Notification::create("Could not save bindings", NotificationIcon::Error)->show();
    }
    return static_cast<bool>(saved);
}

Chord chord(KeyboardInputData const& data) { return {static_cast<int>(data.key), data.modifiers.value}; }
std::string chordText(Chord value) {
    return Keybind(static_cast<enumKeyCodes>(value.key), KeyboardModifier(value.modifiers)).toString();
}
bool isMenuKey(Chord value) {
    for (auto const& key : Mod::get()->getSettingValue<std::vector<Keybind>>("open-menu")) {
        if (value == Chord{static_cast<int>(key.key), key.modifiers.value}) return true;
    }
    return false;
}
bool modifierKey(int key) {
    return key == 0x10 || key == 0x11 || key == 0x12 || key == 0x5b || key == 0x5c ||
           (key >= 0xa0 && key <= 0xa5);
}

struct Entry {
    fs::path path;
    std::string id;
    std::string name;
    bool missing = false;
};

class BindsPopup final : public Popup {
    static constexpr size_t pageSize = 6;
    std::vector<Entry> m_catalog;
    std::vector<size_t> m_visible;
    size_t m_page = 0;
    std::string m_search;
    std::string m_capture;
    TextInput* m_searchInput = nullptr;
    CCNode* m_rows = nullptr;
    CCLabelBMFont* m_status = nullptr;
    CCLabelBMFont* m_pageLabel = nullptr;
    bool m_onlyBound = false;

    CCMenuItemSpriteExtra* button(CCMenu* menu, std::string const& text, CCPoint at,
                                 SEL_MenuHandler callback, int tag = 0, float width = 60.f) {
        auto* sprite = ButtonSprite::create(text.c_str(), "bigFont.fnt", "GJ_button_01.png");
        sprite->setScale(std::min(.52f, width / sprite->getContentWidth()));
        auto* result = CCMenuItemSpriteExtra::create(sprite, this, callback);
        result->setPosition(at);
        result->setTag(tag);
        menu->addChild(result);
        return result;
    }
    void status(std::string const& text) {
        m_status->setString(text.c_str());
        m_status->limitLabelWidth(398.f, .42f, .2f);
    }
    void scan() {
        m_catalog.clear();
        std::set<std::string> seen;
        auto add = [&](fs::path path, bool missing) {
            std::error_code ec;
            auto canonical = fs::weakly_canonical(path, ec);
            if (!ec) path = canonical;
            auto id = utf8(path);
            if (!seen.insert(id).second) return;
            auto name = utf8(path.filename());
            m_catalog.push_back({std::move(path), std::move(id), std::move(name), missing});
        };
        if (auto* xdbot = Loader::get()->getLoadedMod("zilko.xdbot")) {
            auto folder = xdbot->getSettingValue<fs::path>("macros_folder");
            std::error_code ec;
            fs::directory_iterator it(folder, fs::directory_options::skip_permission_denied, ec), end;
            while (!ec && it != end) {
                std::error_code itemError;
                if (it->is_regular_file(itemError) && macroExtension(utf8(it->path().extension())))
                    add(it->path(), false);
                it.increment(ec);
            }
            if (ec) status("Could not read xdBot's macro folder");
        }
        // Keep saved bindings visible if a macro was moved/deleted; never silently discard them.
        for (auto const& item : bindings.items()) {
            auto path = fromUtf8(item.path);
            std::error_code ec;
            bool missing = !fs::is_regular_file(path, ec);
            add(path, missing);
        }
        std::sort(m_catalog.begin(), m_catalog.end(), [](Entry const& a, Entry const& b) {
            auto an = lowerAscii(a.name), bn = lowerAscii(b.name);
            return an == bn ? a.id < b.id : an < bn;
        });
        filter();
    }
    void filter() {
        m_visible.clear();
        auto query = lowerAscii(m_search);
        for (size_t i = 0; i < m_catalog.size(); ++i) {
            auto const& item = m_catalog[i];
            if ((!m_onlyBound || bindings.keyFor(item.id)) &&
                lowerAscii(item.name).find(query) != std::string::npos) m_visible.push_back(i);
        }
        const size_t pages = std::max<size_t>(1, (m_visible.size() + pageSize - 1) / pageSize);
        m_page = std::min(m_page, pages - 1);
        drawRows();
    }
    void drawRows() {
        m_rows->removeAllChildren();
        auto* menu = CCMenu::create();
        menu->setPosition({0, 0});
        m_rows->addChild(menu, 1);
        const size_t first = m_page * pageSize;
        for (size_t row = 0; row < pageSize && first + row < m_visible.size(); ++row) {
            const auto index = m_visible[first + row];
            auto const& item = m_catalog[index];
            const float y = 213.f - static_cast<float>(row) * 27.f;
            auto* background = CCLayerColor::create({0, 0, 0, 65}, 400.f, 25.f);
            background->setPosition({20.f, y - 12.5f});
            m_rows->addChild(background);
            auto* name = CCLabelBMFont::create(item.name.c_str(), "chatFont.fnt");
            name->setAnchorPoint({0.f, .5f});
            name->setPosition({26.f, y});
            name->limitLabelWidth(219.f, .6f, .2f);
            if (item.missing) name->setColor({255, 130, 130});
            m_rows->addChild(name);
            auto key = bindings.keyFor(item.id);
            auto label = m_capture == item.id ? "..." : key ? chordText(*key) : "Bind";
            button(menu, label, {285.f, y}, menu_selector(BindsPopup::onBind), static_cast<int>(index), 67.f);
            button(menu, "Load", {353.f, y}, menu_selector(BindsPopup::onLoad), static_cast<int>(index), 44.f);
            button(menu, "X", {402.f, y}, menu_selector(BindsPopup::onClear), static_cast<int>(index), 21.f);
        }
        if (m_visible.empty()) {
            auto* empty = CCLabelBMFont::create("No macros", "bigFont.fnt");
            empty->setScale(.55f);
            empty->setPosition({220, 151});
            m_rows->addChild(empty);
        }
        const auto pages = std::max<size_t>(1, (m_visible.size() + pageSize - 1) / pageSize);
        m_pageLabel->setString(fmt::format("{} / {}", m_page + 1, pages).c_str());
    }
    Entry const* entry(CCObject* sender) const {
        auto* node = static_cast<CCNode*>(sender);
        int index = node->getTag();
        return index >= 0 && static_cast<size_t>(index) < m_catalog.size() ? &m_catalog[index] : nullptr;
    }
    void onBind(CCObject* sender) {
        if (auto* item = entry(sender)) {
            m_searchInput->defocus();
            m_capture = item->id;
            status("Press a key | Esc: cancel | Delete: clear");
            drawRows();
        }
    }
    void onClear(CCObject* sender) {
        if (auto* item = entry(sender)) {
            bindings.clear(item->id);
            m_capture.clear();
            if (saveBindings()) status("");
            filter();
        }
    }
    void onLoad(CCObject* sender);
    void onPrevious(CCObject*) { if (m_page) --m_page; drawRows(); }
    void onNext(CCObject*) { if ((m_page + 1) * pageSize < m_visible.size()) ++m_page; drawRows(); }
    void onRefresh(CCObject*) { m_capture.clear(); status(""); scan(); }
    void onFilter(CCObject*) { m_onlyBound = !m_onlyBound; m_page = 0; filter(); status(m_onlyBound ? "Bound macros" : "All macros"); }

    bool init() {
        if (!Popup::init(440.f, 300.f)) return false;
        setTitle("Macro Binds");
        readBindings();
        m_status = CCLabelBMFont::create("", "chatFont.fnt");
        m_status->setPosition({220, 17});
        m_mainLayer->addChild(m_status);
        m_pageLabel = CCLabelBMFont::create("1 / 1", "chatFont.fnt");
        m_pageLabel->setScale(.55f);
        m_pageLabel->setPosition({220, 48});
        m_mainLayer->addChild(m_pageLabel);
        m_rows = CCNode::create();
        m_mainLayer->addChild(m_rows);
        m_searchInput = TextInput::create(270.f, "Search macros", "chatFont.fnt");
        m_searchInput->setPosition({155, 249});
        m_searchInput->setScale(.86f);
        m_searchInput->setCommonFilter(CommonFilter::Any);
        m_searchInput->setCallback([this](std::string const& value) { m_search = value; m_page = 0; filter(); });
        m_mainLayer->addChild(m_searchInput);
        auto* controls = CCMenu::create();
        controls->setPosition({0, 0});
        m_mainLayer->addChild(controls);
        button(controls, "All / Bound", {343, 249}, menu_selector(BindsPopup::onFilter), 0, 114);
        button(controls, "<", {168, 48}, menu_selector(BindsPopup::onPrevious), 0, 25);
        button(controls, ">", {272, 48}, menu_selector(BindsPopup::onNext), 0, 25);
        button(controls, "Refresh", {368, 48}, menu_selector(BindsPopup::onRefresh), 0, 78);
        scan();
        if (!ensureBridge()) status(bridgeError());
        return true;
    }
    void onClose(CCObject* sender) override {
        m_capture.clear();
        if (openPopup == this) openPopup = nullptr;
        Popup::onClose(sender);
    }
public:
    ~BindsPopup() override { if (openPopup == this) openPopup = nullptr; }
    static BindsPopup* create() {
        auto* popup = new BindsPopup;
        if (!popup->init()) { delete popup; return nullptr; }
        popup->autorelease();
        return popup;
    }
    void close() { onClose(nullptr); }
    bool capturing() const { return !m_capture.empty(); }
    void capture(Chord value) {
        if (value.key == KEY_Escape) { m_capture.clear(); status(""); drawRows(); return; }
        if (value.key == KEY_Delete || value.key == KEY_Backspace) bindings.clear(m_capture);
        else {
            if (modifierKey(value.key)) return;
            if (value.key <= 0 || value.key > 255) { status("Choose a keyboard key"); return; }
            if (isMenuKey(value)) { status("This key opens the menu; choose another key"); return; }
            bindings.assign(m_capture, value);
        }
        m_capture.clear();
        if (saveBindings()) status("");
        filter();
    }
};

void queueLoad(fs::path path) {
    Loader::get()->queueInMainThread([path = std::move(path)] {
        if (!Mod::get()->getSettingValue<bool>("enabled")) return;
        std::string error;
        try {
            if (loadOnly(path, error)) return;
        } catch (std::exception const& exception) {
            error = "Macro load failed";
            log::error("Macro Binds: {}", exception.what());
        }
        log::warn("Macro Binds: {}", error);
        Notification::create(error, NotificationIcon::Error)->show();
    });
}
void BindsPopup::onLoad(CCObject* sender) {
    if (auto* item = entry(sender)) queueLoad(item->path);
}

void showMenu() {
    if (openPopup) { openPopup->close(); return; }
    if (auto* play = PlayLayer::get(); play && !play->m_isPaused) play->pauseGame(true);
    if (auto* popup = BindsPopup::create()) { openPopup = popup; popup->show(); }
}

bool hasModal() {
    auto* scene = CCDirector::sharedDirector()->getRunningScene();
    if (!scene) return true;
    for (auto* child : CCArrayExt<CCNode*>(scene->getChildren())) {
        if (child->isVisible() && typeinfo_cast<FLAlertLayer*>(child)) return true;
    }
    return false;
}

ListenerResult onKeyboard(KeyboardInputData& data) {
    const int key = static_cast<int>(data.key);
    if (data.action == KeyboardInputData::Action::Release)
        return gate.release(key) ? ListenerResult::Stop : ListenerResult::Propagate;
    // Windows sends a fresh Press (not Repeat) after focus returns, even if its
    // previous release was lost when Alt-Tabbing. Clear only that stale press.
    if (data.action == KeyboardInputData::Action::Press && gate.consumed(key)) gate.release(key);
    if (gate.consumed(key)) return ListenerResult::Stop;
    if (data.action != KeyboardInputData::Action::Press) return ListenerResult::Propagate;
    if (openPopup && openPopup->capturing()) {
        // Geode's event already contains raw modifier state. Consume Alt too,
        // otherwise xdBot's default Alt menu bind would interrupt capture.
        gate.press(key);
        if (modifierKey(key)) return ListenerResult::Stop;
        openPopup->capture(chord(data));
        return ListenerResult::Stop;
    }
    if (CCIMEDispatcher::sharedDispatcher()->hasDelegate()) return ListenerResult::Propagate;
    if (!openPopup && hasModal()) return ListenerResult::Propagate;
    if (isMenuKey(chord(data))) {
        gate.press(key);
        Loader::get()->queueInMainThread([] { showMenu(); });
        return ListenerResult::Stop;
    }
    if (openPopup || !Mod::get()->getSettingValue<bool>("enabled")) return ListenerResult::Propagate;
    readBindings();
    auto path = bindings.pathFor(chord(data));
    if (!path) return ListenerResult::Propagate;
    gate.press(key);
    queueLoad(fromUtf8(*path));
    return ListenerResult::Stop;
}
}

$execute {
    // Runs before Geode forwards keys to xdBot's default bindings (including K).
    KeyboardInputEvent().listen(&mb::onKeyboard, -10000).leak();
}

class $modify(MacroBindsPause, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();
        auto* menu = CCMenu::create();
        menu->setID("sennot.macro_binds/menu");
        menu->setPosition({0, 0});
        auto* sprite = ButtonSprite::create("Binds", "bigFont.fnt", "GJ_button_01.png");
        sprite->setScale(.48f);
        auto* button = CCMenuItemSpriteExtra::create(sprite, this, menu_selector(MacroBindsPause::onBinds));
        auto size = CCDirector::sharedDirector()->getWinSize();
        button->setPosition({size.width - 40.f, 22.f});
        menu->addChild(button);
        addChild(menu, 100);
    }
    void onBinds(CCObject*) { mb::showMenu(); }
};
