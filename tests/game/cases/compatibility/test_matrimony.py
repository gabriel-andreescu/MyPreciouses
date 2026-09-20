import pytest

from ...support.session import actor, assigned


@pytest.mark.compatibility("UpgradableMatrimony")
def test_upgraded_matrimony_rings(rings):
    p, menu = rings.p, rings.menu
    p("Actor", "UnequipAll", self_form="0x14")
    rings.settings(
        bPlayerAlwaysEquipBondOfMatrimonyOnLeftRingFinger=0, bEnableLeftRing=1
    )
    upgrades = [
        rings.client.form_id(i, "UpgradableBondOfMatrimony.esp")
        for i in (0x801, 0x802, 0x804)
    ]
    for ring in upgrades:
        rings.add(ring)
        menu.equip(ring, 3)
        menu.close()
        rings.wait(
            lambda s, ring=ring: (
                assigned(s, ring, 3)
                and sum(
                    v["target"] == 3 and v["thirdPersonGeometry"] > 0
                    for v in actor(s)["visuals"]
                )
                == 1
            ),
            "An upgraded Matrimony ring has no third-person model",
        )
        p("Game", "ForceFirstPerson")
        p("Actor", "DrawWeapon", self_form="0x14")
        rings.wait(
            lambda s: (
                sum(
                    v["target"] == 3 and v["firstPersonGeometry"] > 0
                    for v in actor(s)["visuals"]
                )
                == 1
            ),
            "An upgraded Matrimony ring has no first-person model",
        )
        p("Actor", "UnequipAll", self_form="0x14")
    rings.checkpoint(
        "All three Matrimony upgrades support the selector and both model views"
    )
    rings.settings(
        bPlayerAlwaysEquipBondOfMatrimonyOnLeftRingFinger=1, bEnableLeftRing=0
    )
    for ring in upgrades:
        menu.select(ring)
        menu.ui("InvokeInt", "_root.Menu_mc.SetPlatform", 1)
        menu.ui("InvokeIntA", "_root.Menu_mc.AttemptEquip", [0, 0])
        rings.wait(
            lambda s, ring=ring: not s["selectorOpen"] and assigned(s, ring, 3),
            "The Matrimony rule did not place an upgraded ring on the disabled left ring finger",
        )
        menu.close()
        p("Actor", "UnequipAll", self_form="0x14")
    rings.checkpoint(
        "The player Matrimony rule includes all upgrades and overrides the disabled finger"
    )
    rings.settings(
        bEnableNpcSupport=1, bNpcAlwaysEquipBondOfMatrimonyOnLeftRingFinger=1
    )
    npc = rings.npc()
    npc_id = int(npc, 16)
    p("Actor", "SetRelationshipRank", [{"form": "0x14"}, 4], npc)
    for ring in upgrades:
        rings.add(ring, actor_id=npc)
        state = rings.wait(
            lambda s, ring=ring: (
                assigned(s, ring, 3, npc_id)
                and sum(
                    v["target"] == 3 and v["thirdPersonGeometry"] > 0
                    for v in actor(s, npc_id)["visuals"]
                )
                == 1
            ),
            "The NPC Matrimony rule did not attach an upgraded ring on the disabled left ring finger",
        )
        assignment = next(
            a for a in state["assignments"] if a["actor"] == npc_id and a["target"] == 3
        )
        assert (
            sum(
                e["source"] == assignment["effectSource"]
                for e in actor(state, npc_id)["effects"]
            )
            == 4
        )
        p("ObjectReference", "RemoveItem", [{"form": f"0x{ring:08X}"}, 1, True], npc)
        rings.wait(
            lambda s: not any(a["actor"] == npc_id for a in s["assignments"]),
            "Removing the NPC ring retained its assignment",
        )
        effects = rings.call("inspect", {"kind": "effects", "formId": npc})[
            "activeEffects"
        ]
        assert not any(e["spell"]["formType"] == "ENCH" for e in effects)
    rings.checkpoint(
        "All Matrimony upgrades use the NPC left-ring rule and clean up after removal"
    )
