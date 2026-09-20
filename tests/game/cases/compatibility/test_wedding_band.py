import pytest

from ...support.session import actor, assigned


@pytest.mark.compatibility("WeddingBand")
def test_slot_51_wedding_band(rings):
    p, menu = rings.p, rings.menu
    p("Actor", "UnequipAll", self_form="0x14")
    rings.settings(
        bEnableLeftRing=1, bPlayerAlwaysEquipBondOfMatrimonyOnLeftRingFinger=0
    )
    ring = 0xC5809
    assert p("Armor", "GetSlotMask", self_form="0xC5809") == 0x200000, (
        "WeddingBand.esp must supply the slot-51 override"
    )
    enchantment = rings.client.form_id(0x805, "WeddingBand.esp")
    assert (
        int(p("Armor", "GetEnchantment", self_form="0xC5809")["formId"], 16)
        == enchantment
    )
    before = {
        av: p("Actor", "GetActorValueMax", [av], "0x14")
        for av in ("Health", "Magicka", "Stamina")
    }
    rings.add(ring)
    classified = next(i for i in rings.state()["inventory"] if i["formId"] == ring)
    assert classified["isRing"] and classified["hasRingModel"]
    for target in (0, 3, 8):
        menu.equip(ring, target)
        menu.close()
        rings.wait(
            lambda s, target=target: (
                assigned(s, ring, target)
                and sum(
                    v["target"] == target and v["thirdPersonGeometry"] > 0
                    for v in actor(s)["visuals"]
                )
                == 1
            ),
            "The wedding band did not move to the chosen finger",
        )
        p("Game", "ForceFirstPerson")
        p("Actor", "DrawWeapon", self_form="0x14")
        rings.wait(
            lambda s, target=target: (
                sum(
                    v["target"] == target and v["firstPersonGeometry"] > 0
                    for v in actor(s)["visuals"]
                )
                == 1
            ),
            "The wedding band has no first-person model",
        )
        for av, base in before.items():
            assert p("Actor", "GetActorValueMax", [av], "0x14") == base + 10
    rings.save("MyPreciousesTest_WeddingBand")
    rings.restore("MyPreciousesTest_WeddingBand")
    assert assigned(rings.state(), ring, 8)
    for av, base in before.items():
        assert p("Actor", "GetActorValueMax", [av], "0x14") == base + 10
    p("Actor", "UnequipAll", self_form="0x14")
    rings.wait(lambda s: not s["assignments"], "UnequipAll retained the wedding band")
    for av, base in before.items():
        assert p("Actor", "GetActorValueMax", [av], "0x14") == base
    rings.checkpoint(
        "The slot-51 wedding band supports both hands, both model views, independent bonuses and save/load cleanup"
    )
    rings.settings(
        bEnableNpcSupport=1,
        bNpcAlwaysEquipBondOfMatrimonyOnLeftRingFinger=1,
        bEnableLeftRing=0,
    )
    npc = rings.npc()
    npc_id = int(npc, 16)
    p("Actor", "SetRelationshipRank", [{"form": "0x14"}, 4], npc)
    health = p("Actor", "GetActorValueMax", ["Health"], npc)
    rings.add(ring, actor_id=npc)
    rings.wait(
        lambda s: (
            assigned(s, ring, 3, npc_id)
            and sum(
                v["target"] == 3 and v["thirdPersonGeometry"] > 0
                for v in actor(s, npc_id)["visuals"]
            )
            == 1
        ),
        "The NPC wedding band did not use the reserved left ring finger",
    )
    assert p("Actor", "GetActorValueMax", ["Health"], npc) == health + 10
    p("ObjectReference", "RemoveItem", [{"form": "0xC5809"}, 1, True], npc)
    rings.wait(
        lambda s: not any(a["actor"] == npc_id for a in s["assignments"]),
        "Removing the NPC wedding band retained its assignment",
    )
    assert p("Actor", "GetActorValueMax", ["Health"], npc) == health
    rings.checkpoint(
        "The NPC slot-51 wedding band respects the Matrimony rule and clears its bonus after removal"
    )
