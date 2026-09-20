#include "../src/core.hpp"
#include <cassert>
#include <iostream>

int main() {
    mb::Bindings bindings;
    const mb::Chord k{0x4b, 0}, ctrlK{0x4b, 2}, l{0x4c, 0};
    bindings.assign("C:/macros/one.gdr", k);
    bindings.assign("C:/macros/two.gdr", ctrlK);
    assert(bindings.pathFor(k) == "C:/macros/one.gdr");
    assert(bindings.pathFor(ctrlK) == "C:/macros/two.gdr");
    // Identical keys move rather than dispatching multiple loads.
    bindings.assign("C:/macros/two.gdr", k);
    assert(bindings.items().size() == 1);
    assert(!bindings.keyFor("C:/macros/one.gdr"));
    assert(!bindings.pathFor(ctrlK));
    bindings.assign("C:/macros/two.gdr", l);
    assert(!bindings.pathFor(k));
    assert(bindings.pathFor(l) == "C:/macros/two.gdr");
    // Invalid persisted entries cannot take away an existing key.
    bindings.assign("", k);
    bindings.assign("C:/macros/bad.gdr", {0, 0});
    assert(bindings.items().size() == 1);
    mb::Bindings restored;
    for (auto const& item : bindings.items()) restored.assign(item.path, item.chord);
    assert(restored.pathFor(l) == bindings.pathFor(l));
    bindings.clear("C:/macros/two.gdr");
    assert(bindings.items().empty());
    assert(restored.items().size() == 1);
    // Modifier release, closing the popup and rebinding must not leak key-up.
    mb::PressGate gate;
    gate.press(k.key);
    for (int i = 0; i < 1000; ++i) assert(gate.consumed(k.key));
    bindings.assign("C:/macros/new.gdr", k);
    assert(gate.release(k.key));
    assert(!gate.release(k.key));
    assert(!gate.consumed(k.key));
    gate.press(k.key); gate.reset(); assert(!gate.consumed(k.key));
    for (auto extension : {".gdr", ".GDR2", ".json", ".xd", ".slc"}) assert(mb::macroExtension(extension));
    for (auto extension : {".dll", ".cbf", ".zcb", ".txt", ""}) assert(!mb::macroExtension(extension));
    // Only the success notification of our scoped load is hidden. Warnings,
    // errors, ordinary xdBot loads and later notifications retain their behavior.
    mb::LoadContext context;
    int successNotification = 0, errorNotification = 0;
    context.created(&successNotification, "Macro Loaded");
    assert(!context.succeeded && !context.mute(&successNotification));
    context.begin();
    assert(context.active && !context.succeeded);
    context.created(&errorNotification, "Cannot load macro");
    assert(!context.succeeded && !context.mute(&errorNotification));
    context.created(&successNotification, "Macro Loaded");
    assert(context.succeeded && !context.mute(&errorNotification));
    assert(context.mute(&successNotification));
    assert(!context.mute(&successNotification));
    context.end();
    assert(!context.active && !context.mute(&successNotification));
    context.begin();
    assert(!context.succeeded && !context.notification);
    context.created(&successNotification, "Macros Loaded");
    context.end();
    assert(!context.mute(&successNotification));
    std::cout << "PASS bindings: key reassignment, modifiers, invalid entries, copy/restore, release ownership, repeat gate, macro extensions\n";
    std::cout << "PASS scoped load: success-only suppression, single consumption, errors remain visible, state cleared between loads\n";
}
