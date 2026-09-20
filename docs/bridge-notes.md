# Supported binary and integration

This is an independent add-on, not an xdBot rebuild. No xdBot source or binary is shipped.

## Evidence from the supplied Windows DLL

See `binary-profile.json` for the complete fingerprint. All addresses below are module-relative RVAs, so ASLR does not matter.

| Item | RVA / offset | Evidence |
| --- | --- | --- |
| Global accessor | `0x10d00` | Returns the singleton at RVA `0x33fc88`; called by the loader and playback toggle. |
| MacroCell factory | `0x1d82a0` | Allocates the native cell, initializes the path/name/date and three parent arguments, autoreleases the result. Called by the macro-list builder. |
| MacroCell load | `0x1d9840` | Reads the cell's file, calls native importers, installs the macro, resets indices, emits Macro Loaded on success. |
| Macro playback toggle | `0xb16c0` | Called by the load routine at `0x1da45f` when outside PlayLayer and not already playing. |
| State | Global + `0x728` | `1` = recording, `2` = playing, `0` = idle. Compared in both load and playback routines. Read only by this add-on. |
| Notification create import | `0x2af130` | Geode `Notification::create(ZStringView, NotificationIcon, float)`. |
| Notification show import | `0x2af4f0` | Geode `Notification::show()`. |
| Success text | `0x2a3eca` | Literal `Macro Loaded`, referenced by the native load success path. |

The add-on creates a real native MacroCell through the factory and a real, invisible CCLayer for its parent calls. It does not fake an object layout, copy a private Macro struct, retain stale cell addresses, or write directly to the global macro/state. Windows release std::string and std::filesystem::path ABI sizes are asserted. Three Geode-managed hooks observe the native success notification, hide its show call, and ignore automatic playback toggles only during the synchronous load scope. Outside that scope each hook forwards normally.

Key events use Geode 5.10's KeyboardInputEvent with priority -10000, ahead of the built-in keybind dispatcher. File loading is queued onto the game's main thread. Per-input work is an in-memory binding lookup; there are no polling threads or frame-by-frame directory scans. Parsing time and accuracy remain those of xdBot's native importer/player. F8 is the menu entry point; no PauseLayer hook or button is installed.

Saved paths are UTF-8; conversion to the Windows filesystem uses the C++20 char8_t path constructor. A cancellable worker enumerates xdBot's configured macro/autosave folders, existing game/save-directory defaults and the optional saved Folder selection. It searches subfolders, skips directory links and deduplicates overlapping roots. Completion is queued on the game thread and ignored after the popup closes or a newer scan starts. Text search operates on the cached list. Six rows per page bound UI node count. Paths and scan errors are logged; the exact cause of the user's original empty list is not confirmed without their runtime folder values.

## Reference material checked

- Supplied `zilko.xdbot.dll`: authoritative for all private addresses and importer behavior.
- [Geode SDK 5.10.0](https://github.com/geode-sdk/geode/tree/v5.10.0): Popup, TextInput, hooks, key events, mod settings and persistence APIs.
- [GD 2.2081 bindings](https://github.com/geode-sdk/bindings/tree/main/bindings/2.2081): PauseLayer, PlayLayer, ButtonSprite.
- [Older public xdBot code](https://github.com/ZiLko/xdBot): used to interpret the class names/call flow, then checked against the supplied DLL. It is not treated as the source of the supplied 2.7.7 binary.

## Reproducible checks

```sh
g++ -std=c++20 -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=all tests/core_test.cpp -o core-test
./core-test
g++ -std=c++20 -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=all tests/catalog_test.cpp -o catalog-test
./catalog-test
python tests/verify_target.py /path/to/the/supplied.geode
```

These checks do not constitute a Windows mod build or an in-game validation. GitHub Actions includes the host tests followed by the actual Windows/Geode build. UI interaction, native C++ calls, error handling for each macro format and coexistence with the user's installed mods still require an in-game smoke test.
