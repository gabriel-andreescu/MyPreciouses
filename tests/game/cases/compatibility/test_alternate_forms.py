import time

import pytest
from bmk.testing import wait_for

from ...support.session import assigned


@pytest.mark.compatibility("AFKU")
@pytest.mark.parametrize("form", ["Werewolf", "VampireLord"])
def test_equipment_changes_in_alternate_forms(rings, form):
    p, menu = rings.p, rings.menu
    mods = rings.call("inspect", {"kind": "mods"})
    assert (
        sum(
            m["name"] == "Alternate Forms Keep Armors.esp" for m in mods["lightPlugins"]
        )
        == 1
    )
    p("Actor", "UnequipAll", self_form="0x14")
    original = int(p("Actor", "GetRace", self_form="0x14")["formId"], 16)
    health, silver, namira, gold = 0xFCEFD, 0x3B97C, 0x2C37B, 0x1CF2B
    arcana = rings.client.form_id(0x1DB9B, "Dragonborn.esm")
    for ring in (health, silver, namira, gold, arcana):
        rings.add(ring)
    for target, ring in enumerate((health, silver, namira, arcana)):
        menu.equip(ring, target)
    menu.close()
    if form == "Werewolf":
        rings.call(
            "console", {"command": f"setpqv C00 PlayerOriginalRace 0x{original:08X}"}
        )
        rings.transform(0x92C48, 0xCDD84)
    else:
        rings.transform(
            rings.client.form_id(0x283B, "Dawnguard.esm"),
            rings.client.form_id(0x283A, "Dawnguard.esm"),
        )
    # AFKU restores controls after its configured transformation delay.
    time.sleep(20)
    menu.equip(silver, 8)
    menu.equip(gold, 2)
    rings.wait(
        lambda s: not any(b["source"] == namira for b in s["scriptBindings"]),
        "Replacing a suspended ring retained its script binding",
    )
    menu.equip(gold, 0)
    menu.open_selector(gold, 0)
    menu.ui("Invoke", f"{menu.selector}.EquipSelection")
    rings.wait(
        lambda s: not assigned(s, gold, 0), "The replacement ring did not unequip"
    )
    menu.equip(gold, 5)
    menu.close()
    for ring in (namira, arcana):
        rings.call("console", {"command": f"player.removeitem {ring:08X} 1"})
    wait_for(
        lambda: (
            p("ObjectReference", "GetItemCount", [{"form": "0x2C37B"}], "0x14") == 0
        ),
        message="Namira was not removed during transformation",
    )
    save = f"MyPreciousesTest_{form}Equipment"
    rings.save(save)
    rings.restore(save)
    rings.revert(
        "PlayerWerewolfQuest" if form == "Werewolf" else "DLC1PlayerVampireQuest",
        original,
    )
    state = rings.wait(
        lambda s: (
            assigned(s, silver, 8)
            and assigned(s, gold, 5)
            and len(s["assignments"]) == 2
            and not s["scriptBindings"]
        ),
        "Returning from alternate form overwrote equipment changes or restored removed rings",
    )
    assert not any(a["target"] == 0 for a in state["assignments"])
    wait_for(
        lambda: not p("Actor", "HasPerk", [{"form": "0xEE5C3"}], "0x14"),
        message="Removing Namira while transformed retained cannibalism",
    )
    rings.checkpoint(
        f"AFKU {form} equipment moves, replacement, unequip and removal survive save/load and return"
    )
