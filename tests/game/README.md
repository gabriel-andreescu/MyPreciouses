# In-game tests

Pytest tests for ring selection, models, enchantments, scripts, special rings,
transformations, inventory confiscation and save persistence through
[DevBench](https://github.com/alandtse/devbench).

## Requirements

- A running Skyrim game with MyPreciouses and its matching MCM addon.
- [SkyUI](https://www.nexusmods.com/skyrimspecialedition/mods/12604),
  [MCM Helper](https://www.nexusmods.com/skyrimspecialedition/mods/53000),
  [powerofthree's Papyrus Extender](https://www.nexusmods.com/skyrimspecialedition/mods/22854)
  and [DevBench](https://github.com/alandtse/devbench/releases) 1.18.1 or newer.
  Install their listed requirements and choose binaries for your Skyrim runtime.
- Python 3.11 or newer and the BMK Python dependencies declared by this project.
- A fresh Nord test character in QASmoke, with an empty Favorites list, no
  silver rings or health rings in their inventory, and no Namira cannibalism
  perk.
- Keyboard bindings for Quick Inventory, Quick Magic, Favorites and Shout.
- English interface text for the inventory label assertions.

The suite changes equipment, settings, skills and quest state. Use a dedicated
test save. Avoid input while the suite operates menus. DevBench input works with
Skyrim in the background.

## Install the test tools

Install the locked development dependencies:

```powershell
uv sync --locked
```

See BMK's
[DevBench fixture reference](https://github.com/gabriel-andreescu/BethesdaModKit/blob/main/docs/mod-authors/tooling/skyrim/devbench.md)
for client and pytest behavior.

## Prepare a baseline

From Skyrim's main menu, run `prepare.json` through DevBench's `scenario` tool.
It enters QASmoke and creates `MyPreciousesTest_QASmoke`. Create the baseline
with the same mods and load order used by the tests. This replaces an existing
save with that name.

The installed command can run the scenario too:

```powershell
uv run devbench-scenario tests/game/prepare.json `
  --url http://127.0.0.1:8920 `
  --output test-results/prepare.json
```

Set the endpoint if DevBench does not use its default port. Dismiss any startup
or missing-content dialogs before running tests.

## Run

`--settings` is the physical file backing `MCM/Settings/MyPreciouses.ini`. With
MO2, this may be in Overwrite or a mod directory. Launch the mod once so the
file exists.

```powershell
uv run pytest tests/game --game-tests `
  --devbench-url http://127.0.0.1:8920 `
  --settings 'C:/path/to/MCM/Settings/MyPreciouses.ini' `
  --baseline MyPreciousesTest_QASmoke `
  --game-results test-results/game --junitxml=test-results/game.xml -x
```

The default selection runs the core tests. Use `--collect-only` to list them,
`-k custom` to filter by name, or pass a specific test file. Without
`--game-tests`, tests are skipped. Run serially, without pytest-xdist workers.

## Cold restart

Run the same command with `--restart-phase prepare`. This creates checkpoint
saves and `restart-state.json` in the results directory. Quit and restart Skyrim
with the same mods, then use `--restart-phase verify` with the same results
directory. Both phases are needed to check shared ring forms, NPC bonuses and
custom enchantments across a process restart.

## Optional cases

See [fixture setup](fixtures.md) for the vanilla-menu, Warlock and compatibility
cases, including downloads, load order, baseline requirements and cleanup.

For attachment tests with Skyrim's standard allocator, disable Engine Fixes'
allocator overrides, restart Skyrim and run the core cases. Restore your
allocator settings afterward.

## Results and cleanup

Per-test results are under `--game-results`, including `transcript.json` and
`settings-before.ini`. Tests restore settings and reload the baseline. If a run
is interrupted, restore the settings from that backup and reload the baseline
before continuing. Named `MyPreciousesTest_*` saves remain after the run.
