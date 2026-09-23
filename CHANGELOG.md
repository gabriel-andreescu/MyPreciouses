# Changelog

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

### Fixed

- Allow the player to equip rings taken from NPC outfits on other fingers

## [1.0.0] - 2026-09-20

### Added

- Apply the Bond of Matrimony placement settings to Upgradable Bond of Matrimony
- Add an MCM toggle for debug logging

### Changed

- **Breaking change:** Rename the DLL, MCM plugin and settings paths from
  LeftHandRingsSKSE to MyPreciouses
- **Breaking change:** Rename the debug INI setting to `bDebugLogging`
- Reduce generated ring records by reusing them across NPCs and when
  re-equipping rings
- Reduce CPU and memory overhead when equipping rings and updating inventory
  labels

### Fixed

- Support Skyrim 1.7.104
- Preserve Frostmoon ring effects during werewolf transformations
- Apply Beast and Erudite ring bonuses in Vampire Lord form
- Preserve ring changes made during transformations
- Prevent crashes when equipping scripted rings
- Preserve scripted ring effects across finger changes and save/load
- Clear scripted ring effects when dropping or transferring an extra ring
- Prevent missing or duplicated NPC ring enchantments after loading a save or
  returning to an area
- Prevent freezes and excessive logging when an NPC cannot equip a ring
- Keep extra rings invisible when changing equipment during Ghostwalk
- Restore extra rings after bathing with the optional Diziet bath patch
- Restore finger selection for valid ring models rejected since 0.4.5
- Keep plain rings selectable alongside custom-enchanted copies
- Allow a single ring to move between fingers on opposite hands
- Honor finger selection when confirming a ring with the keyboard in Favorites

## [0.4.6] - 2026-06-22

### Added

- Let functional virtual rings satisfy worn keyword conditions

## [0.4.5] - 2026-06-21

### Changed

- Move Bond of Matrimony left ring finger controls to a Special Rings MCM page
  and support always equipping it for players and NPCs

### Fixed

- Ignore inactive outfit-managed ring copies when auto-equipping NPC rings
- Ignore mod-defined slot 36 accessories without wearable ring model evidence
- Reject mixed accessory models during ring model classification
- Keep finger targeting working for rings that also include non-ring armor
  models

## [0.4.4] - 2026-06-17

### Added

- Add player Bond of Matrimony left hand ring finger support when the left hand
  ring finger slot is disabled

### Fixed

- Prefer index fingers before thumb slots when auto-equipping NPC rings
- Prevent Vanilla UI player inventory rows from becoming undefined

## [0.4.3] - 2026-06-09

### Fixed

- Restore ring equip and left equip actions in inventory menus after the 0.4.2
  crash fix

## [0.4.2] - 2026-06-08

### Fixed

- Prevent crashes when opening follower or container inventories with rings
  recently added or removed by another mod

## [0.4.1] - 2026-06-08

### Fixed

- Prevent crashes and freezes in Skyrim VR when opening containers or refreshing
  inventory ring rows

## [0.4.0] - 2026-06-07

### Added

- Support vanilla UI
- Add MCM controls for choosing enabled virtual ring slots
- Support follower and NPC virtual rings

### Changed

- Improve MCM organization and preserve existing settings during updates

### Fixed

- Prevent virtual ring effects from replaying during load and inventory
  refreshes
- Ignore non-ring ClothingRing items that have no wearable ring model
- Clear custom inventory ring controls from non-ring rows
- Refresh inventory ring rows after vanilla slot swaps
- Hide redundant index finger labels on inventory ring rows
- Account for both hands when placing, replacing, and rendering multi-finger
  rings
- Keep existing ring labels visible in finger selector replacement previews and
  use negative coloring for occupied slots
- Clear extra rings when UnequipAll runs, then restore them after race
  transformations such as werewolf or vampire lord

## [0.3.6] - 2026-06-01

### Fixed

- Place mod-added rings correctly when equipped on the vanilla right index
  finger
- Prevent some mod-added rings from stacking on the vanilla right index finger
  instead of replacing each other
- Prevent VR crashes when checking which fingers a ring model uses

## [0.3.5] - 2026-05-30

### Added

- Support mod-added rings with the ClothingRing keyword regardless of body slot

### Changed

- Improve finger selector menu previews, action labels, and multi-finger ring
  handling

### Fixed

- Ignore alternate-form body addons when checking ring finger coverage (eg:
  Alternate Forms Keep Utility)
- Ignore negligible incidental skin weights when checking ring finger coverage
- Allow multi-finger rings to start from valid fingers in the finger selector
- Allow single-copy virtual rings to be moved between fingers in the finger
  selector

## [0.3.4] - 2026-05-30

### Fixed

- Properly refresh inventory ring equip markers on all runtimes
- Show the finger selection modifier hint in the SkyUI VR inventory footer

## [0.3.3] - 2026-05-29

### Fixed

- Support Frostmoon rings on virtual fingers
- Ignore zero-magnitude enchantments in strength scaling

## [0.3.2] - 2026-05-29

### Fixed

- Prevent inventory menu crashes when equipping rings through the finger
  selector

## [0.3.1] - 2026-05-28

### Fixed

- Center the finger selector in wider inventory menus such as Dear Diary
- Remove inactive virtual ring enchantment effects when unequipping
- Prevent crashes when attaching virtual ring visuals
- Prevent quest-locked rings from being unequipped from the vanilla right index
  finger

## [0.3.0] - 2026-05-28

### Added

- Add translation support for UI text
- Add configurable finger selection modifier keybinds
- Show finger selection modifier hint in the SkyUI inventory footer
- Show left and right equip controls for rings in inventory
- Show ring finger assignments in inventory
- Add ring finger selection menu
- Add ring enchantment strength modes
- Add cosmetic mode for extra rings

### Changed

- Improve unequip flow for assigned rings
- Replace left-hand ring clones with slotless virtual rings

### Fixed

- Prevent a crash when left-equipping rings from the inventory menu
- Use unique inventory identity for player-enchanted virtual rings

## [0.2.0] - 2026-05-22

### Added

- Add controller support

### Fixed

- Flag MCM addon as ESPFE
- Avoid overwriting MCM user settings during updates
- Play left-hand ring equip sounds
- Recognize left-hand ring clones in GetEquipped conditions (eg: Ring of
  Namira's Feed prompt)
- VR compatibility
- Prevent duplicate body visuals from hybrid-slot rings (eg: CC's Ring of
  Disrobing)

## [0.1.1] - 2026-05-21

### Added

- Initial release.
