import time

import pytest
from bmk.testing import wait_for

from ...support.session import actor, assigned


@pytest.mark.compatibility("DizietBath")
@pytest.mark.parametrize(
    "slow,keep_namira", [(False, False), (True, False), (True, True)]
)
def test_bath_restoration_and_changed_equipment(rings, slow, keep_namira):
    p, menu = rings.p, rings.menu
    p("Actor", "UnequipAll", self_form="0x14")
    silver, health_ring, namira = 0x3B97C, 0xFCEFD, 0x2C37B
    perk = {"form": "0xEE5C3"}
    health = p("Actor", "GetActorValueMax", ["Health"], "0x14")
    p("Actor", "ForceActorValue", ["Enchanting", 35.0], "0x14")
    rings.call("console", {"command": "playerenchantobject 3b97c 493aa"})
    time.sleep(0.3)
    menu.equip(silver, 6)
    menu.close()
    enchantment = p("WornObject", "GetEnchantment", [{"form": "0x14"}, 0, 64])["formId"]
    name = "MyPreciouses Test Bath Ring"
    assert p("WornObject", "SetDisplayName", [{"form": "0x14"}, 0, 64, name, True])
    p("Actor", "UnequipAll", self_form="0x14")
    for ring in (silver, health_ring, namira, 0x877C9):
        rings.add(ring)
    menu.equip(silver, 0, enchantment_id=0)
    menu.equip(silver, 1, name)
    menu.equip(health_ring, 2)
    menu.equip(namira, 8)
    menu.equip(0x877C9, 6)
    menu.close()
    equipped_health = p("Actor", "GetActorValueMax", ["Health"], "0x14")
    rings.call("console", {"command": "coc dzundresstestingcell"})
    wait_for(
        lambda: (
            rings.call("inspect", {"kind": "scene"})["cell"]["editorId"]
            == "dzundresstestingcell"
        ),
        message="The bath test cell did not load",
    )
    time.sleep(3)
    rings.call(
        "console",
        {"command": f"setpqv dz_undress_MCM_menu slow_unequip {str(slow).lower()}"},
    )
    if keep_namira:
        for function, value in (
            ("GoToState", "state_keywords"),
            ("OnInputAcceptST", "DaedricArtifact"),
            ("GoToState", ""),
        ):
            p("dz_undress_MCM_menu_script", function, [value], "dz_undress_MCM_menu")
        assert p("Form", "HasKeywordString", ["DaedricArtifact"], "0x2C37B")
        assert not p("Form", "HasKeywordString", ["DaedricArtifact"], "0xFCEFD")
    p("ObjectReference", "SetPosition", [1021.0, -298.0, 0.0], "0x14")
    assert len(rings.state()["assignments"]) == 4
    rings.save("MyPreciousesTest_BeforeBath")
    for changed in (False, True):
        rings.restore("MyPreciousesTest_BeforeBath", "dzundresstestingcell")
        p("ObjectReference", "SetPosition", [1792.0, -624.0, 112.0], "0x14")
        rings.wait(
            lambda s: (
                len(s["assignments"]) == int(keep_namira)
                and len(s["scriptBindings"]) == int(keep_namira)
                and (not keep_namira or assigned(s, namira, 8))
                and actor(s)["rightWorn"] == 0
                and all(
                    (v["thirdPersonGeometry"] > 0) == (keep_namira and v["target"] == 8)
                    for v in actor(s)["visuals"]
                )
            ),
            "The bath did not remove rings according to its keyword exclusions",
        )
        wait_for(
            lambda: p("Actor", "GetActorValueMax", ["Health"], "0x14") == health,
            message="Bathing retained ring enchantments",
        )
        wait_for(
            lambda: p("Actor", "HasPerk", [perk], "0x14") == keep_namira,
            message="Bathing did not preserve Namira's perk only when excluded",
        )
        if changed:
            p("ObjectReference", "RemoveItem", [{"form": "0xFCEFD"}, 1, True], "0x14")
            rings.add(0x877CA)
            menu.equip(0x877CA, 0)
            menu.close()
        rings.save("MyPreciousesTest_InsideBath")
        rings.restore("MyPreciousesTest_InsideBath", "dzundresstestingcell")
        p("ObjectReference", "SetPosition", [1021.0, -298.0, 0.0], "0x14")
        rings.wait(
            lambda s, changed=changed: (
                assigned(s, silver, 1)
                and assigned(s, namira, 8)
                and assigned(s, 0x877C9, 6)
                and len(s["assignments"]) == (3 if changed else 4)
                and all(
                    v["thirdPersonGeometry"] > 0
                    for v in actor(s)["visuals"]
                    if v["target"] in ({0, 1, 8} if changed else {0, 1, 2, 8})
                )
            ),
            "Leaving the bath did not restore available ring selections",
        )
        state = rings.state()
        assert (
            sum(
                a["target"] == 1 and a["enchantment"] == int(enchantment, 16)
                for a in state["assignments"]
            )
            == 1
        )
        wait_for(
            lambda: p("Actor", "HasPerk", [perk], "0x14"),
            message="The bath did not restore Namira script effects",
        )
        if changed:
            assert assigned(state, 0x877CA, 0)
            assert (
                p("ObjectReference", "GetItemCount", [{"form": "0xFCEFD"}], "0x14") == 0
            )
            assert (
                p("Actor", "GetActorValueMax", ["Health"], "0x14")
                == equipped_health - 20
            )
        else:
            assert p("Actor", "GetActorValueMax", ["Health"], "0x14") == equipped_health
            assert assigned(state, silver, 0)
        p("Actor", "UnequipAll", self_form="0x14")
        p("MyPreciouses", "RestoreExtraRings", [{"form": "0x14"}])
        rings.wait(
            lambda s: not s["assignments"] and not s["scriptBindings"],
            "The consumed bath snapshot restored twice",
        )
        wait_for(
            lambda: (
                p("Actor", "GetActorValueMax", ["Health"], "0x14") == health
                and not p("Actor", "HasPerk", [perk], "0x14")
            ),
            message="Restored bath rings left effects after unequipping",
        )
        rings.checkpoint(
            f"Diziet bath restores named, enchanted and scripted rings across save/load, with slow={slow}, keep Namira={keep_namira}, changed equipment={changed}"
        )
