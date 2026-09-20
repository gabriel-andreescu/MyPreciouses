import time

import pytest
from bmk.testing import wait_for

from ...support.session import actor


@pytest.mark.compatibility("Berserkyr")
def test_berserkyr_hircine_and_bears_paw(rings):
    p, menu = rings.p, rings.menu
    plugin = "Tales of Skyrim - Berserkyr.esp"
    mods = rings.call("inspect", {"kind": "mods"})
    assert sum(m["name"] == plugin for m in mods["lightPlugins"]) == 1
    p("Actor", "UnequipAll", self_form="0x14")
    form = rings.client.form_id
    ritual, tutorial, wears_ring = [
        f"0x{form(i, plugin):08X}" for i in (0x82A, 0x899, 0x8A1)
    ]
    power = form(0x8A0, plugin)
    spell = {"form": f"0x{power:08X}"}
    race = form(0x1E17B, "Dragonborn.esm")
    original = int(p("Actor", "GetRace", self_form="0x14")["formId"], 16)
    hircine, gold = 0x2AC60, 0x1CF2B
    targets = (0, 1, 2, 3, 4, 5, 7, 8, 9)
    # Skip informational tutorial prompts and select werebear eligibility independently of lycanthropy.
    p("GlobalVariable", "SetValue", [1.0], tutorial)
    rings.call("console", {"command": "setpqv C00 PlayerHasBeastBlood false"})
    p("GlobalVariable", "SetValue", [0.0], ritual)
    rings.add(hircine)
    menu.equip(hircine, 3)
    menu.close()
    assert not p("Actor", "HasSpell", [spell], "0x14"), (
        "Hircine granted werebear power before the ritual"
    )
    assert not p("Actor", "HasSpell", [{"form": "0xF8306"}], "0x14"), (
        "Hircine granted werewolf power to a non-werewolf"
    )
    p("Actor", "UnequipAll", self_form="0x14")
    p("GlobalVariable", "SetValue", [1.0], ritual)
    assert p("GlobalVariable", "GetValue", self_form=ritual) == 1
    for target in (3, 6, 8):
        menu.equip(hircine, target)
        menu.close()
        wait_for(
            lambda: p("Actor", "HasSpell", [spell], "0x14"),
            message=f"Hircine lost werebear power on target {target}",
        )
    rings.save("MyPreciousesTest_BerserkyrHircine")
    rings.restore("MyPreciousesTest_BerserkyrHircine")
    assert p("Actor", "HasSpell", [spell], "0x14")
    rings.settings(iExtraRingMode=1)
    wait_for(
        lambda: not p("Actor", "HasSpell", [spell], "0x14"),
        message="Cosmetic mode retained werebear power",
    )
    rings.settings(iExtraRingMode=0)
    wait_for(
        lambda: p("Actor", "HasSpell", [spell], "0x14"),
        message="Functional mode did not restore werebear power",
    )
    rings.checkpoint(
        "Berserkyr Hircine eligibility, native/virtual moves, cosmetic mode and save/load"
    )
    rings.add(gold, 8)
    for target in targets:
        if target != 8:
            menu.equip(gold, target)
    menu.close()
    before = rings.state()["assignments"]
    for cycle in (1, 2):
        rings.transform(power, race)
        assert p("GlobalVariable", "GetValue", self_form=wears_ring) == 1
        rings.wait(
            lambda s: (
                not s["assignments"]
                and sum(b["suspended"] for b in s["scriptBindings"]) == 1
            ),
            "Werebear transformation did not suspend ring equipment",
        )
        save = f"MyPreciousesTest_Werebear{cycle}"
        rings.save(save)
        rings.restore(save)
        assert actor(rings.state())["race"] == race
        rings.revert("BOS_PlayerWerebearQuest", original)
        wait_for(
            lambda: not p("Quest", "IsRunning", self_form="BOS_PlayerWerebearQuest"),
            message="The werebear return sequence did not finish",
            timeout=15,
        )
        # Berserkyr calls Shutdown again five seconds after changing the race back.
        time.sleep(6)
        state = rings.wait(
            lambda s: (
                len(s["assignments"]) == 9
                and sum(v["thirdPersonGeometry"] > 0 for v in actor(s)["visuals"]) == 9
            ),
            "Returning from werebear form lost rings or models",
        )
        assert state["assignments"] == before
        wait_for(
            lambda: p("Actor", "HasSpell", [spell], "0x14"),
            message="Returning lost reusable Hircine power",
        )
        rings.checkpoint(
            f"Berserkyr Hircine transformation {cycle} restores nine rings after saving in werebear form"
        )
    p("Actor", "UnequipAll", self_form="0x14")
    wait_for(
        lambda: not p("Actor", "HasSpell", [spell], "0x14"),
        message="Unequipping Hircine retained werebear power",
    )
    perk = {"form": "0xEE5C3"}
    rings.add(0x2C37B)
    menu.equip(0x2C37B, 0)
    for target in targets:
        if target != 0:
            menu.equip(gold, target)
    menu.close()
    wait_for(
        lambda: p("Actor", "HasPerk", [perk], "0x14"),
        message="Namira did not grant cannibalism",
    )
    paw = form(0x81D, plugin)
    rings.add(paw)
    p("Actor", "EquipItem", [{"form": f"0x{paw:08X}"}, False, True], "0x14")
    rings.wait(
        lambda s: actor(s)["race"] == race,
        "Eating Bear's Paw did not transform the player",
        25,
    )
    assert p("GlobalVariable", "GetValue", self_form=wears_ring) == 0
    rings.wait(lambda s: not s["assignments"], "Bear's Paw did not suspend equipment")
    p("ObjectReference", "RemoveItem", [{"form": "0x2C37B"}, 1, True], "0x14")
    wait_for(
        lambda: not p("Actor", "HasPerk", [perk], "0x14"),
        message="Removing Namira in werebear form retained cannibalism",
    )
    rings.save("MyPreciousesTest_WerebearPaw")
    rings.restore("MyPreciousesTest_WerebearPaw")
    rings.revert("BOS_PlayerWerebearQuest", original)
    wait_for(
        lambda: not p("Quest", "IsRunning", self_form="BOS_PlayerWerebearQuest"),
        message="The mushroom transformation did not finish returning",
        timeout=15,
    )
    rings.wait(
        lambda s: (
            len(s["assignments"]) == 8
            and not any(a["source"] == 0x2C37B for a in s["assignments"])
            and not s["scriptBindings"]
        ),
        "Returning restored removed Namira or lost other rings",
    )
    assert not p("Actor", "HasPerk", [perk], "0x14")
    rings.checkpoint(
        "Berserkyr Bear's Paw transformation preserves ring removal through save/load and return"
    )
