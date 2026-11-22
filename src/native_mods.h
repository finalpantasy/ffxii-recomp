#pragma once
// Native module loader; see include/ffxii_mod.h.

namespace ffxii {

// Loads every enabled mod's native modules and runs their ffxii_mod_init.
// Set as GameConfig::before_entry.
void LoadNativeModules();

}  // namespace ffxii
