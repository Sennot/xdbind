#include "../src/catalog.hpp"
#include <cassert>
#include <chrono>
#include <fstream>
#include <iostream>

int main() {
    namespace fs = std::filesystem;
    const auto base = fs::current_path() / ("catalog-test-" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directory(base);
    struct Cleanup {
        fs::path path;
        ~Cleanup() { std::error_code ec; fs::remove_all(path, ec); }
    } cleanup{base};
    auto macros = base / "macros";
    auto autosaves = base / "autosaves";
    auto nested = macros / fs::path(u8"папка с пробелами");
    auto external = base / "chosen-folder";
    fs::create_directories(nested);
    fs::create_directory(autosaves);
    fs::create_directory(external);
    auto write = [](fs::path const& file) { std::ofstream(file) << "fixture"; };
    write(macros / "one.gdr");
    write(nested / fs::path(u8"макрос.GDR2"));
    write(autosaves / "auto.gdr.json");
    write(external / "manual.slc");
    write(macros / "one.gdr.cbf"); // Sidecars are not extra macros.
    write(macros / "notes.txt");
    fs::create_directory(macros / "folder.gdr");
    std::atomic_bool cancelled{false};
    auto found = mb::scanFolders({macros, autosaves, external, nested, macros / "."}, cancelled);
    assert(found.files.size() == 4 && found.issues.empty());
    for (auto const& file : found.files) {
        assert(file.is_absolute());
        assert(fs::is_regular_file(file));
        assert(mb::fromUtf8(mb::utf8(file)) == file);
    }
    auto partial = mb::scanFolders({base / "missing", macros / "notes.txt", macros}, cancelled);
    assert(partial.files.size() == 2 && partial.issues.size() == 2);
    assert(mb::scanFolders({{}}, cancelled).files.empty()); // Never scan cwd for an empty setting.
    cancelled.store(true);
    assert(mb::scanFolders({macros}, cancelled).files.empty());
    cancelled.store(false);
    std::error_code ec;
    fs::create_directory_symlink(macros, nested / "cycle", ec);
    if (!ec) {
        auto cyclic = mb::scanFolders({macros}, cancelled);
        assert(cyclic.files.size() == 2 && cyclic.issues.empty());
    }
    fs::remove(macros / "one.gdr");
    write(macros / "new.xd");
    auto refreshed = mb::scanFolders({macros}, cancelled);
    assert(refreshed.files.size() == 2);
    for (auto const& file : refreshed.files) assert(file.filename() != "one.gdr");
    std::cout << "PASS catalog: nested and Unicode paths, autosaves, manual folder, duplicate roots, sidecars, errors, cancellation, directory cycle, refresh\n";
}
