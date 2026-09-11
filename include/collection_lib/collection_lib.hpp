#pragma once

// dusklight-collection-lib
//
// A reusable Collection Screen extension for Dusklight Twilight Princess mods.
// Embeds into a mod at BUILD time (static library): adds custom equippable
// swords / shields / tunics to the pause Collection screen, optionally the
// vanilla starter gear (wooden sword / Ordon clothes / Ordon shield), an
// "Unequip" action, a second collection page (Heart Piece + Mirror of
// Twilight), widescreen layout shifts and full cursor/navigation support for
// every registered slot.
//
// Typical consumer:
//     #include <collection_lib/collection_lib.hpp>
//     collectionlib_set_options({...});
//     collectionlib_register_slot({ CE_SWORD, 5, "My Sword", "...", ... });
//     collectionlib_init(hook_svc, log_svc, save_svc, mod_ctx);
//     ... collectionlib_update() every frame, collectionlib_shutdown() on teardown.

#include "collection_common.hpp"
#include "custom_equip.hpp"

#include "mods/svc/save.h"

// --- lifecycle ---------------------------------------------------------------

// Registers every hook the library needs and restores the previously equipped
// custom gear from the save. Register your slots (collectionlib_register_slot)
// and set your options (collectionlib_set_options) BEFORE calling this.
ModResult collectionlib_init(const HookService* hook_svc, const LogService* log_svc,
                             const SaveService* save_svc, ModContext* mod_ctx,
                             ModError* error = nullptr);

// Call once per frame from the mod's update.
void collectionlib_update();

// Call from the mod's shutdown.
void collectionlib_shutdown();

// --- slot registration -------------------------------------------------------

// The library re-runs slot registration every time the collection screen is
// built (it must be idempotent - custom_equip_register is, per kind+item).
// Provide ONE function that registers all of the mod's slots:
//
//     static void my_register_slots() {
//         collectionlib_register_slot({ CE_SWORD, 5, "My Sword", ... });
//     }
//     collectionlib_set_register_callback(&my_register_slots);
//
// It is invoked once from collectionlib_init (before the equipped state is
// restored from the save) and again on every screen build.
void collectionlib_set_register_callback(void (*fn)());

// Convenience: register a custom equip slot directly at a row position
// (1 = leftmost column of the row). The def's `item` field is overridden.
// Returns the slot id (>= 0).
int collectionlib_add_sword_slot(u8 item, const CustomEquipDef& def);
int collectionlib_add_shield_slot(u8 item, const CustomEquipDef& def);
int collectionlib_add_tunic_slot(u8 item, const CustomEquipDef& def);

// Auto-placement: register a custom equip slot at the NEXT FREE column of the
// row (first column not claimed by another registered slot of that kind).
// Returns the slot id, or -1 when the row (4 columns) is already full.
int collectionlib_add_next_sword_slot(const CustomEquipDef& def);
int collectionlib_add_next_shield_slot(const CustomEquipDef& def);
int collectionlib_add_next_tunic_slot(const CustomEquipDef& def);

// Register one custom equip slot. See CustomEquipDef in custom_equip.hpp for
// the full field list; the model/icon files are resolved from the OWNING MOD's
// res/ directory via the ResourceService. Returns the slot id (>= 0).
// Registration is idempotent per (kind, item) so calling it from an
// init that can re-run is safe.
inline int collectionlib_register_slot(const CustomEquipDef& def) {
    return custom_equip_register(def);
}

// Number of registered slots.
inline int collectionlib_slot_count() {
    return custom_equip_count();
}

// --- per-kind equipped state -------------------------------------------------
// At most one custom item per kind (sword / shield / tunic) can be equipped.

// Equip the slot with this id (custom_equip_activate).
inline void collectionlib_activate(int id) { custom_equip_activate(id); }

// Clear the equipped custom item of a kind (back to vanilla gear).
inline void collectionlib_clear(CustomEquipKind kind) { custom_equip_clear(kind); }

// Is a custom item of this kind currently equipped?
inline bool collectionlib_active(CustomEquipKind kind) { return custom_equip_active(kind); }

// The active slot id of a kind, or -1.
inline int collectionlib_active_id(CustomEquipKind kind) { return custom_equip_active_id(kind); }
