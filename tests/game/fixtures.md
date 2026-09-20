# Optional test fixtures

Run these cases with the connection and settings parameters shown in the
[main test instructions](README.md#run).

`--compatibility NAME` appends a fixture to the core run. Repeat the option for
additional fixtures. Add `--compatibility-only` to run just those fixtures, for
example
`--compatibility-only --compatibility ORF --compatibility UpgradableMatrimony`.

## Vanilla menus

Add `--vanilla-menus` to the run command for the vanilla-menu cases instead of
the core suite. Install
[SkyUI Vanilla Menus](https://www.nexusmods.com/skyrimspecialedition/mods/93039)
using **SkyUI Full Vanilla Menus - unpacked 1.3b**. Let its loose menu files
override SkyUI and enable `Nyr_SkyUIVanillaMenus.esp` after SkyUI and MCM
Helper. Finish its initialization before running the tests.

These cases check ordinary inventory names, all fingers, ring labels, custom
names, save/load, Favorites and cancellation through DevBench input.

Add `--buffered-input-tests` with Skyrim in the foreground for the original
keyboard test. It checks Shift/RB hints, Shift selection, arrow navigation, E,
Tab and Escape through SKSE buffered input. Modifier detection reads the
keyboard device state directly. DevBench button events do not update that state
or deliver the selector's Scaleform arrow-key input. Controller button art is
checked without requiring a controller. Physical controller input needs separate
testing. Run compatibility fixtures separately from vanilla-menu tests.

## Compatibility cases

For the Fishing Creation's Warlock Ring, add `--warlock` to run its blocking
cases instead of the core suite. Enable `ccbgssse001-fish.esm` and assign **Left
Attack/Block** to a keyboard key in Skyrim's Controls menu. Restore your
preferred binding afterward. The suite uses the current binding without changing
it.

These cases hold the block control with a drawn sword and check the ring on
native and extra fingers. They assert one conditional ward and 25 ward power
while blocking, zero ward power on release, save/load, selector unequip, and
removal from inventory while blocking. Blocking again without the ring must not
recreate its effects.

Add `--compatibility NAME` for installed fixtures:

| Case                  | Additional requirements                                                                                                                                                                                                                                                                                                                       |
| --------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `ORF`                 | [Outfit Recognition Framework](https://www.nexusmods.com/skyrimspecialedition/mods/163502) and **ORF Rings** from its optional files. Enable `Outfit Recognition Framework.esp` and `ORF Rings.esp`.                                                                                                                                          |
| `UpgradableMatrimony` | [Upgradable Bond of Matrimony](https://www.nexusmods.com/skyrimspecialedition/mods/103185), with `UpgradableBondOfMatrimony.esp` enabled.                                                                                                                                                                                                     |
| `WeddingBand`         | [Bond of Matrimony (Wedding Band Enhancement)](https://www.nexusmods.com/skyrimspecialedition/mods/68942), with `WeddingBand.esp` enabled. Checks its left-handed slot-51 model, bonuses, NPC placement and save/load.                                                                                                                        |
| `AFKU`                | [Alternate Forms Keep Utility](https://www.nexusmods.com/skyrimspecialedition/mods/123204), with `Alternate Forms Keep Armors.esp` enabled. Exercises equipment changes during both beast forms, save/load and return.                                                                                                                        |
| `Elewin`              | [Elewin Jewelry SSE](https://www.nexusmods.com/skyrimspecialedition/mods/21350), with `Elewin Jewelry.esp` enabled. Checks that navel piercings equip without finger selection.                                                                                                                                                               |
| `SaintsAndSeducers`   | The Saints & Seducers Creation, with `ccBGSSSE025-AdvDSGS.esm` loaded. Checks the Ring of Disrobing's torso-slot conflicts and excludes extra body meshes from virtual ring attachments.                                                                                                                                                      |
| `Mysticism`           | [Mysticism - A Magic Overhaul](https://www.nexusmods.com/skyrimspecialedition/mods/27839), with `MysticismMagic.esp` enabled. Checks Ghostwalk on existing and replaced rings, then natural expiration, in both model views.                                                                                                                  |
| `Legacy`              | [Legacy of the Dragonborn](https://www.nexusmods.com/skyrimspecialedition/mods/11802), with `LegacyoftheDragonborn.esm` enabled and its Crusader quest unstarted in the baseline. Checks Sir Amiel, Mentor, Phynaster and Warlock originals and replicas, quest progression, NPC transfers and save/load.                                     |
| `SandsOfTime`         | [Ultimate Deadly Encounters and Spawns](https://www.nexusmods.com/skyrimspecialedition/mods/3093), with `SOTFull.esp` enabled. Activate it through MCM and finish its startup prompts before saving the baseline. Wail of Pain must still be locked. Checks the Ring of Escape script on player and NPC equipment, consumption and save/load. |

For `--compatibility OblivionArtifacts`, install
[Oblivion Artifact Pack SE](https://www.nexusmods.com/skyrimspecialedition/mods/10644)
V3 with `WZOblivionArtifacts.esp` enabled. The baseline must not contain its
Sorcerer's Ring or Ring of Transmutation. These cases check repeated hand
changes, effect counts, magicka bonuses, save/load and removal. Sorcerer's Ring
also checks that inventory actions and loading preserve the age of its
absorption effects.

For `--compatibility ValSerano`, install
[Val Serano](https://www.nexusmods.com/skyrimspecialedition/mods/103669) 2.6.3
with `AX ValSerano.esp` enabled. Choose installer patches only for mods enabled
in your test setup. The cases spawn Val and make each of his regular and
sleeping outfits current through `Actor.SetOutfit`, keeping the other outfit
assigned but inactive. They check that the inactive outfit's ring stays
unequipped while a separately acquired copy equips, survives save/load and
cleans up when removed. They do not exercise his sleep schedule or dialogue.

For `--compatibility Berserkyr`, install
[Tales of Skyrim - Berserkyr](https://www.nexusmods.com/skyrimspecialedition/mods/103559),
[Custom Skills Framework](https://www.nexusmods.com/skyrimspecialedition/mods/41780)
and
[FormList Manipulator](https://www.nexusmods.com/skyrimspecialedition/mods/74037).
Skyrim 1.7.104 needs the
[FLM Community Port](https://www.nexusmods.com/skyrimspecialedition/mods/189947).
Enable `Tales of Skyrim - Berserkyr.esp`. The cases cover Hircine's werebear
power, repeated transformations with nine rings, saving in werebear form and
removing a scripted ring during a Bear's Paw transformation.

For `--compatibility DizietBath`, install
[Diziet's Player Home Bath Undressing](https://www.nexusmods.com/skyrimspecialedition/mods/30265)
7.1.2.7, select its testing cell in the installer, and install the MyPreciouses
Diziet bath patch after it. Enable `dz_undress_common.esp` and
`dz_undress_testing_cell.esp`. Use the bath mod's default instant unequip mode.
The cases enter and leave its player-only trigger, save while bathing, and check
named rings, enchantments, scripts, removed items and new selections. They also
check that slow undressing leaves extra rings equipped.

Install each fixture's requirements before creating the baseline. Load patches
after their masters. For the upgraded marriage-ring model case, also install
[JS Unique Utopia SE - Rings](https://www.nexusmods.com/skyrimspecialedition/mods/102226)
and its **Upgradable BoM Patch** optional file. Enable both plugins and let the
patch override Upgradable Bond of Matrimony.

Run `--compatibility WeddingBand` with `WeddingBand.esp` winning the original
Bond of Matrimony record. Disable competing marriage-ring overrides for that
run. The case checks the slot and enchantment before testing the ring.

The core cases also work with these optional replacers enabled:

- [DDV Jewelry Replacer](https://www.nexusmods.com/skyrimspecialedition/mods/157252)
  for the finger models and NPC equipment checks. Use its replacer file.
- [Ahzidal's Rings by Ave](https://www.nexusmods.com/skyrimspecialedition/mods/42599)
  for the Arcana and Necromancy model checks.
- [Ave's Bond of Matrimony](https://www.nexusmods.com/skyrimspecialedition/mods/38110)
  for the original marriage ring.
- [Ring of Namira - Improved](https://www.nexusmods.com/skyrimspecialedition/mods/87260)
  and its **Survival Mode** optional patch for the scripted-ring cases. The
  patch also requires Survival Mode.

Choose the replacer whose models you want to test. A replacer overwritten by
another mod is not being exercised. These files are downloaded separately and
are not bundled with the suite.

The ORF case evaluates real `GetEquipped` and `WornHasKeyword` engine
conditions. It restores its temporary condition list before leaving the case.
