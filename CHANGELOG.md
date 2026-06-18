# Changelog

## 0.4.5 - Unreleased

- fix: ignore inactive outfit-managed ring copies when auto-equipping NPC rings
- fix: ignore mod-defined slot 36 accessories without wearable ring model evidence
- fix: reject mixed accessory models during ring model classification

## 0.4.4 - 2026-06-17

- feat: add player Bond of Matrimony left hand ring finger support when the left hand ring finger slot is disabled
- fix: prefer index fingers before thumb slots when auto-equipping NPC rings
- fix: prevent Vanilla UI player inventory rows from becoming undefined

## 0.4.3 - 2026-06-09

- fix: restore ring equip and left equip actions in inventory menus after the 0.4.2 crash fix

## 0.4.2 - 2026-06-08

- fix: prevent crashes when opening follower or container inventories with rings recently added or removed by another
  mod

## 0.4.1 - 2026-06-08

- fix: prevent crashes and freezes in Skyrim VR when opening containers or refreshing inventory ring rows

## 0.4.0 - 2026-06-07

- feat: improve MCM organization and preserve existing settings during updates
- feat: support vanilla UI
- feat: add MCM controls for choosing enabled virtual ring slots
- feat: support follower and NPC virtual rings
- fix: prevent virtual ring effects from replaying during load and inventory refreshes
- fix: ignore non-ring ClothingRing items that have no wearable ring model
- fix: clear custom inventory ring controls from non-ring rows
- fix: refresh inventory ring rows after vanilla slot swaps
- fix: hide redundant index finger labels on inventory ring rows
- fix: account for both hands when placing, replacing, and rendering multi-finger rings
- fix: keep existing ring labels visible in finger selector replacement previews and use negative coloring for occupied
  slots
- fix: clear extra rings when UnequipAll runs, then restore them after race transformations such as werewolf or vampire
  lord

## 0.3.6 - 2026-06-01

- fix: place mod-added rings correctly when equipped on the vanilla right index finger
- fix: prevent some mod-added rings from stacking on the vanilla right index finger instead of replacing each other
- fix: prevent VR crashes when checking which fingers a ring model uses

## 0.3.5 - 2026-05-30

- feat: improve finger selector menu previews, action labels, and multi-finger ring handling
- feat: support mod-added rings with the ClothingRing keyword regardless of body slot
- fix: ignore alternate-form body addons when checking ring finger coverage (eg: Alternate Forms Keep Utility)
- fix: ignore negligible incidental skin weights when checking ring finger coverage
- fix: allow multi-finger rings to start from valid fingers in the finger selector
- fix: allow single-copy virtual rings to be moved between fingers in the finger selector

## 0.3.4 - 2026-05-30

- fix: properly refresh inventory ring equip markers on all runtimes
- fix: show the finger selection modifier hint in the SkyUI VR inventory footer

## 0.3.3 - 2026-05-29

- fix: support Frostmoon rings on virtual fingers
- fix: ignore zero-magnitude enchantments in strength scaling

## 0.3.2 - 2026-05-29

- fix: prevent inventory menu crashes when equipping rings through the finger selector

## 0.3.1 - 2026-05-28

- fix: center the finger selector in wider inventory menus such as Dear Diary
- fix: remove inactive virtual ring enchantment effects when unequipping
- fix: prevent crashes when attaching virtual ring visuals
- fix: prevent quest-locked rings from being unequipped from the vanilla right index finger

## 0.3.0 - 2026-05-28

- feat: add translation support for UI text
- feat: add configurable finger selection modifier keybinds
- feat: show finger selection modifier hint in the SkyUI inventory footer
- feat: improve unequip flow for assigned rings
- feat: show left and right equip controls for rings in inventory
- feat: show ring finger assignments in inventory
- feat: add ring finger selection menu
- feat: add ring enchantment strength modes
- feat: add cosmetic mode for extra rings
- feat: replace left-hand ring clones with slotless virtual rings
- fix: prevent a crash when left-equipping rings from the inventory menu
- fix: use unique inventory identity for player-enchanted virtual rings

## 0.2.0 - 2026-05-22

- feat: add controller support
- fix: flag MCM addon as ESPFE
- fix: avoid overwriting MCM user settings during updates
- fix: play left-hand ring equip sounds
- fix: recognize left-hand ring clones in GetEquipped conditions (eg: Ring of Namira's Feed prompt)
- fix: VR compatibility
- fix: prevent duplicate body visuals from hybrid-slot rings (eg: CC's Ring of Disrobing)

## 0.1.1 - 2026-05-21

- Initial release.
