import json
from pathlib import Path

import pytest
from bmk.testing import preserved_file

from .support.session import RingSession


def pytest_addoption(parser):
    group = parser.getgroup("my-preciouses")
    group.addoption(
        "--buffered-input-tests",
        action="store_true",
        help="Include SKSE buffered modifier input checks. Requires Skyrim in the foreground",
    )
    group.addoption(
        "--settings", type=Path, help="Physical MCM/Settings/MyPreciouses.ini file"
    )
    group.addoption(
        "--baseline", default="MyPreciousesTest_QASmoke", help="Dedicated baseline save"
    )
    group.addoption(
        "--compatibility",
        action="append",
        default=[],
        help="Enable a named compatibility fixture",
    )
    group.addoption(
        "--compatibility-only",
        action="store_true",
        help="Run only the selected compatibility fixtures",
    )
    group.addoption("--vanilla-menus", action="store_true")
    group.addoption("--warlock", action="store_true")
    group.addoption("--restart-phase", choices=("prepare", "verify"))


def pytest_collection_modifyitems(config, items):
    selected, deselected = [], []
    compat = set(config.getoption("compatibility"))
    special = [
        name
        for name, enabled in (
            ("vanilla", config.getoption("vanilla_menus")),
            ("warlock", config.getoption("warlock")),
            ("restart", config.getoption("restart_phase")),
        )
        if enabled
    ]
    if len(special) > 1 or (special and compat):
        raise pytest.UsageError(
            "Run vanilla menus, Warlock, cold restart and compatibility fixtures separately"
        )
    if config.getoption("compatibility_only") and not compat:
        raise pytest.UsageError("--compatibility-only requires --compatibility")
    available = {
        m.args[0] for item in items for m in item.iter_markers("compatibility")
    }
    unknown = compat - available
    if unknown:
        raise pytest.UsageError(
            f"Unknown compatibility fixtures: {', '.join(sorted(unknown))}"
        )
    for item in items:
        item.add_marker(pytest.mark.game)
        if item.get_closest_marker("buffered_input") and not config.getoption(
            "buffered_input_tests"
        ):
            item.add_marker(
                pytest.mark.skip(
                    reason="Requires --buffered-input-tests and foreground Skyrim"
                )
            )
        required = {m.args[0] for m in item.iter_markers("compatibility")}
        excluded = bool(required - compat)
        if config.getoption("compatibility_only") and not required:
            excluded = True
        if special and not item.get_closest_marker(special[0]):
            excluded = True
        for marker, option in (
            ("vanilla", "vanilla_menus"),
            ("warlock", "warlock"),
            ("restart", "restart_phase"),
        ):
            if item.get_closest_marker(marker) and not config.getoption(option):
                excluded = True
        (deselected if excluded else selected).append(item)
    if deselected:
        config.hook.pytest_deselected(items=deselected)
        items[:] = selected


@pytest.fixture
def rings(request, devbench, game_artifacts):
    settings = request.config.getoption("settings")
    if settings is None:
        raise pytest.UsageError("MyPreciouses tests require --settings")
    baseline = request.config.getoption("baseline")
    if request.config.getoption("restart_phase") == "verify":
        checkpoint = (
            Path(request.config.getoption("game_results")) / "restart-state.json"
        )
        saved = json.loads(checkpoint.read_text(encoding="utf-8"))
        health = devbench.health()
        assert saved["processId"] != health["pid"], (
            "Restart Skyrim between prepare and verify"
        )
        assert saved["executable"] == health["exe"], (
            "Use the same Skyrim installation for both restart phases"
        )
        baseline = saved["baseline"]
    bench = RingSession(
        devbench,
        settings,
        baseline,
        game_artifacts,
        vanilla=bool(request.node.get_closest_marker("vanilla")),
    )
    loaded = False
    with preserved_file(settings, game_artifacts / "settings-before.ini"):
        try:
            bench.restore()
            loaded = True
            mods = devbench.call("inspect", {"kind": "mods"})
            addon = [
                mod for mod in mods["lightPlugins"] if mod["name"] == "MyPreciouses.esp"
            ]
            assert len(addon) == 1, "Enable the MyPreciouses MCM addon for these tests"
            bench.mcm_quest = f"0x{devbench.form_id(0x800, 'MyPreciouses.esp'):08X}"
            bench.settings(
                bAlwaysChooseFinger=1,
                iExtraRingMode=0,
                iEnchantmentStrengthMode=0,
                bUnequipAllClearsExtraRings=1,
            )
            with bench.keyboard:
                yield bench
        finally:
            # Restore disk settings before the baseline reload reads them.
            settings.write_bytes((game_artifacts / "settings-before.ini").read_bytes())
            if loaded:
                bench.restore()
                if bench.mcm_quest:
                    bench.reload_settings()
