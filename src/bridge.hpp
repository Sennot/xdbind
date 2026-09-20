#pragma once
#include <filesystem>
#include <string>

namespace mb {
// These entry points are private to the exact, fingerprinted xdBot DLL.
// Never fall back to guessing addresses for another build.
bool ensureBridge();
std::string const& bridgeError();
bool loadOnly(std::filesystem::path const& path, std::string& error);
}
