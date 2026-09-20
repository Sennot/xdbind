#include "bridge.hpp"
#include "core.hpp"
#include <Geode/Geode.hpp>
#include <Geode/loader/Hook.hpp>
#include <Geode/ui/Notification.hpp>
#include <Windows.h>
#include <bcrypt.h>
#include <array>
#include <cstring>
#include <fstream>

using namespace geode::prelude;

namespace mb {
namespace {
constexpr char dllHash[] = "8252391a4abd9efec83a06b2a7320236e189aa20e7d8b49d26dbf358d2d0126e";
constexpr uintptr_t createRva = 0x1d82a0;
constexpr uintptr_t loadRva = 0x1d9840;
constexpr uintptr_t globalRva = 0x10d00;
constexpr uintptr_t toggleRva = 0xb16c0;
constexpr uintptr_t stateOffset = 0x728;
// Verified import slots: Notification::create(ZStringView, NotificationIcon, float), show().
constexpr uintptr_t notificationCreateIat = 0x2af130;
constexpr uintptr_t notificationShowIat = 0x2af4f0;
using CellCreate = CCNode* (*)(std::filesystem::path, std::string, int64_t, void*, void*, CCLayer*);
using CellLoad = void (*)(CCNode*);
using GlobalGet = std::byte* (*)();
using TogglePlaying = void (*)();
using CreateNotification = Notification* (*)(ZStringView, NotificationIcon, float);
using ShowNotification = void (*)(Notification*);

uintptr_t base = 0;
bool ready = false;
bool attempted = false;
std::string failure;
CreateNotification originalCreate = nullptr;
ShowNotification originalShow = nullptr;
TogglePlaying originalToggle = nullptr;
thread_local LoadContext context;

struct HashHandles {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    ~HashHandles() {
        if (hash) BCryptDestroyHash(hash);
        if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    }
};

std::string fingerprint(std::filesystem::path const& file) {
    std::ifstream stream(file, std::ios::binary);
    if (!stream) return {};
    HashHandles handles;
    if (BCryptOpenAlgorithmProvider(&handles.algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) return {};
    if (BCryptCreateHash(handles.algorithm, &handles.hash, nullptr, 0, nullptr, 0, 0) < 0) return {};
    std::array<unsigned char, 65536> buffer{};
    while (stream) {
        stream.read(reinterpret_cast<char*>(buffer.data()), buffer.size());
        if (stream.gcount() && BCryptHashData(handles.hash, buffer.data(), static_cast<ULONG>(stream.gcount()), 0) < 0) return {};
    }
    if (!stream.eof()) return {};
    std::array<unsigned char, 32> digest{};
    if (BCryptFinishHash(handles.hash, digest.data(), static_cast<ULONG>(digest.size()), 0) < 0) return {};
    std::string result;
    constexpr char hex[] = "0123456789abcdef";
    for (auto byte : digest) { result += hex[byte >> 4]; result += hex[byte & 15]; }
    return result;
}

template<size_t N> bool prefix(uintptr_t rva, unsigned char const (&bytes)[N]) {
    return std::memcmp(reinterpret_cast<void*>(base + rva), bytes, N) == 0;
}

Notification* onNotificationCreate(ZStringView text, NotificationIcon icon, float duration) {
    auto* notification = originalCreate(text, icon, duration);
    context.created(notification, text.view());
    return notification;
}
void onNotificationShow(Notification* notification) {
    if (context.mute(notification)) {
        return; // The autorelease pool disposes of the unshown notification normally.
    }
    originalShow(notification);
}
void onTogglePlaying() {
    if (!context.active) originalToggle();
}

struct LoadScope {
    LoadScope() { context.begin(); }
    ~LoadScope() { context.end(); }
};
}

std::string const& bridgeError() { return failure; }

bool ensureBridge() {
    if (ready) return true;
    if (attempted) return false;
    auto module = GetModuleHandleW(L"zilko.xdbot.dll");
    if (!module) { failure = "xdBot is not loaded"; return false; }
    attempted = true;
    std::array<wchar_t, 32768> modulePath{};
    DWORD count = GetModuleFileNameW(module, modulePath.data(), static_cast<DWORD>(modulePath.size()));
    if (!count || count >= modulePath.size() || fingerprint(modulePath.data()) != dllHash) {
        failure = "Unsupported xdBot build. Use the supplied 2.7.7 DLL.";
        return false;
    }
    base = reinterpret_cast<uintptr_t>(module);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        nt->FileHeader.TimeDateStamp != 1788463614u || nt->OptionalHeader.SizeOfImage != 3731456u ||
        !prefix(createRva, {0x55,0x41,0x57,0x41,0x56,0x56,0x57,0x53,0x48,0x81,0xec,0xd8,0x00,0x00,0x00,0x48}) ||
        !prefix(loadRva, {0x55,0x56,0x57,0x53,0x48,0x81,0xec,0xc8,0x08,0x00,0x00,0x48,0x8d,0xac,0x24,0x80}) ||
        !prefix(globalRva, {0x55,0x48,0x83,0xec,0x30,0x48,0x8d,0x6c,0x24,0x30,0x48,0xc7,0x45,0xf8,0xfe,0xff}) ||
        !prefix(toggleRva, {0x56,0x48,0x83,0xec,0x30,0xe8,0x26,0xad,0xfb,0xff,0xa8,0x01,0x75,0x02,0xeb,0x02})) {
        failure = "xdBot memory differs from the supported build";
        return false;
    }
    // MSVC release ABI used by the supplied clang-cl build, not a recreated Macro layout.
    static_assert(sizeof(std::filesystem::path) == 32);
    static_assert(sizeof(std::string) == 32);
    originalCreate = *reinterpret_cast<CreateNotification*>(base + notificationCreateIat);
    originalShow = *reinterpret_cast<ShowNotification*>(base + notificationShowIat);
    originalToggle = reinterpret_cast<TogglePlaying>(base + toggleRva);
    std::vector<Hook*> installed;
    auto install = [&](auto target, auto detour, char const* name) {
        auto result = Mod::get()->hook(reinterpret_cast<void*>(target), detour, name);
        if (!result) {
            failure = "Could not install xdBot load bridge: " + result.unwrapErr();
            for (auto* hook : installed) {
                auto removed = Mod::get()->disownHook(hook);
                if (!removed) log::error("Could not remove bridge hook: {}", removed.unwrapErr());
            }
            return false;
        }
        installed.push_back(result.unwrap());
        return true;
    };
    if (!install(originalCreate, &onNotificationCreate, "Macro Binds: observe load result")) return false;
    if (!install(originalShow, &onNotificationShow, "Macro Binds: silence own load")) return false;
    if (!install(originalToggle, &onTogglePlaying, "Macro Binds: load without autoplay")) return false;
    failure.clear();
    ready = true;
    log::info("Macro Binds: verified xdBot 2.7.7 bridge ready");
    return true;
}

bool loadOnly(std::filesystem::path const& path, std::string& error) {
    if (!ensureBridge()) { error = failure; return false; }
    if (context.active) { error = "A macro is already loading"; return false; }
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) { error = "Macro file is missing"; return false; }
    const auto size = std::filesystem::file_size(path, ec);
    if (ec || size == 0) { error = "Macro file is empty or unreadable"; return false; }
    auto* global = reinterpret_cast<GlobalGet>(base + globalRva)();
    int state = 0;
    std::memcpy(&state, global + stateOffset, sizeof(state));
    if (state == 1) { error = "Stop recording before loading a macro"; return false; }
    if (state < 0 || state > 2) { error = "Unexpected xdBot state"; return false; }

    // A real CCLayer keeps every native UI virtual call valid. It is never shown.
    auto* owner = CCLayer::create();
    if (!owner) { error = "Could not create the load context"; return false; }
    Ref<CCLayer> keepOwner = owner;
    auto name8 = path.stem().u8string();
    std::string name(name8.begin(), name8.end());
    auto* cell = reinterpret_cast<CellCreate>(base + createRva)(path, name, 0, nullptr, nullptr, owner);
    if (!cell) { error = "Could not create xdBot's macro loader"; return false; }
    Ref<CCNode> keepCell = cell;
    LoadScope scope;
    reinterpret_cast<CellLoad>(base + loadRva)(cell);
    if (!context.succeeded) {
        error = "xdBot could not load this macro; check its error message";
        return false;
    }
    return true;
}
}
