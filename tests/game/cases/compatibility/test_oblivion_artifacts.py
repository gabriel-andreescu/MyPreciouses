import time

import pytest
from bmk.testing import wait_for


@pytest.mark.compatibility("OblivionArtifacts")
@pytest.mark.parametrize(
    "name,local_id,bonus,effect_count",
    [("Sorcerer", 0x34410, 25, 2), ("Transmutation", 0x3477, 50, 3)],
    ids=["Sorcerer", "Transmutation"],
)
def test_oblivion_artifact_effects(rings, name, local_id, bonus, effect_count):
    p, menu = rings.p, rings.menu
    p("Actor", "UnequipAll", self_form="0x14")
    ring = rings.client.form_id(local_id, "WZOblivionArtifacts.esp")
    reference = {"form": f"0x{ring:08X}"}
    enchantment = p("Armor", "GetEnchantment", self_form=reference["form"])["formId"]
    magicka = p("Actor", "GetActorValueMax", ["Magicka"], "0x14")
    rings.add(ring)

    def effects():
        return [
            e
            for e in rings.call("inspect", {"kind": "effects"})["activeEffects"]
            if e["spell"]["formId"] == enchantment
        ]

    for target in (0, 6, 4, 6, 9, 6, 0, 6, 4, 6, 9, 6, 0):
        menu.equip(ring, target)
        menu.close()
        wait_for(
            lambda: (
                p("Actor", "GetActorValueMax", ["Magicka"], "0x14") == magicka + bonus
            ),
            message=f"{name} lost or duplicated its magicka bonus on target {target}",
        )
        assert len(effects()) == effect_count
        assert p("ObjectReference", "GetItemCount", [reference], "0x14") == 1
    save = f"MyPreciousesTest_Oblivion{name}"
    rings.save(save)
    rings.restore(save)
    assert p("Actor", "GetActorValueMax", ["Magicka"], "0x14") == magicka + bonus
    rings.checkpoint(
        f"Oblivion {name} ring preserves effects through repeated hand changes and save/load"
    )
    if name == "Sorcerer":
        wait_for(
            effects,
            lambda items: len(items) == 2 and min(e["elapsed"] for e in items) >= 15,
            "Sorcerer effects did not accumulate elapsed time",
            timeout=30,
        )
        for action in ("Weapon", "Food", "Potion", "Load"):
            before = effects()
            assert len(before) == 2
            age = min(e["elapsed"] for e in before)
            start = time.monotonic()
            if action == "Load":
                rings.save("MyPreciousesTest_SorcererContinuity")
                rings.restore("MyPreciousesTest_SorcererContinuity")
            else:
                item = {"Weapon": 0x12EB7, "Food": 0x64B2E, "Potion": 0x3EADD}[action]
                rings.add(item)
                item_ref = {"form": f"0x{item:08X}"}
                count = p("ObjectReference", "GetItemCount", [item_ref], "0x14")
                menu.select(item)
                menu.ui("InvokeInt", "_root.Menu_mc.SetPlatform", 1)
                menu.ui("InvokeIntA", "_root.Menu_mc.AttemptEquip", [0, 0])
                menu.close()
                if action == "Weapon":
                    assert (
                        p("Actor", "GetEquippedWeapon", [False], "0x14")["formId"]
                        == item_ref["form"]
                    )
                else:
                    assert (
                        p("ObjectReference", "GetItemCount", [item_ref], "0x14")
                        == count - 1
                    )
            after = effects()
            assert len(after) == 2, f"{action} changed the Sorcerer effect count"
            assert time.monotonic() - start < age, (
                "The action took too long to distinguish effect preservation from a restart"
            )
            assert min(e["elapsed"] for e in after) >= age, (
                f"{action} restarted a Sorcerer ring effect"
            )
        rings.checkpoint(
            "Oblivion Sorcerer absorption effects retain their age through inventory actions and save/load"
        )
    p("Actor", "UnequipAll", self_form="0x14")
    wait_for(
        lambda: p("Actor", "GetActorValueMax", ["Magicka"], "0x14") == magicka,
        message=f"{name} retained its magicka bonus after removal",
    )
    wait_for(
        lambda: not effects(),
        message=f"{name} retained enchantment effects after removal",
    )
