import pytest
from bmk.testing import wait_for

from ...support.session import actor, assigned


@pytest.mark.compatibility("Legacy")
def test_legacy_rings_and_crusader_quest(rings):
    p, menu = rings.p, rings.menu
    rings.settings(bEnableNpcSupport=1)
    p("Actor", "UnequipAll", self_form="0x14")
    plugin = "LegacyoftheDragonborn.esm"
    quest = f"0x{rings.client.form_id(0x9211C7, plugin):08X}"
    assert p("Quest", "GetStage", self_form=quest) == 0, (
        "The LOTD test baseline already started the Crusader quest"
    )
    cases = [
        ("Sir Amiel", 0x1662B6),
        ("Mentor", 0x125B03),
        ("Phynaster", 0x125B04),
        ("Warlock", 0x125B02),
        ("Sir Amiel replica", 0xB828),
        ("Mentor replica", 0xB824),
        ("Phynaster replica", 0xB831),
        ("Warlock replica", 0xB82B),
    ]
    for name, local_id in cases:
        ring = rings.client.form_id(local_id, plugin)
        assert ring != 0
        rings.add(ring)
        if local_id == 0x1662B6:
            wait_for(
                lambda: (
                    p("Quest", "GetStage", self_form=quest) == 6
                    and p("Quest", "IsObjectiveCompleted", [17], quest)
                ),
                message="Acquiring Sir Amiel did not advance its quest",
            )
            rings.checkpoint(
                "Acquiring Sir Amiel advances the Crusader quest and completes its objective"
            )
        for target in (3, 6, 8, 0, 3):
            menu.equip(ring, target)
            menu.close()
            p("Game", "ForceFirstPerson")
            p("Actor", "DrawWeapon", self_form="0x14")
            rings.wait(
                lambda s, ring=ring, target=target: (
                    assigned(s, ring, target)
                    and sum(
                        v["target"] == target
                        and v["firstPersonGeometry"] > 0
                        and v["thirdPersonGeometry"] > 0
                        for v in actor(s)["visuals"]
                    )
                    == 1
                ),
                f"{name} has no model on target {target}",
            )
            assert (
                next(i for i in rings.state()["inventory"] if i["formId"] == ring)[
                    "count"
                ]
                == 1
            )
        p("Actor", "UnequipAll", self_form="0x14")
        rings.wait(
            lambda s: not s["assignments"] and actor(s)["rightWorn"] == 0,
            f"{name} remained equipped",
        )
        rings.checkpoint(
            f"{name} moves between native and virtual fingers with both model views"
        )
    amiel = rings.client.form_id(0x1662B6, plugin)
    menu.equip(amiel, 3)
    menu.close()
    npc = rings.npc()
    npc_id = int(npc, 16)
    rings.add(amiel, 2, npc)
    rings.wait(
        lambda s: (
            actor(s, npc_id) is not None
            and actor(s, npc_id)["rightWorn"] == amiel
            and sum(
                a["actor"] == npc_id and a["source"] == amiel for a in s["assignments"]
            )
            == 1
        ),
        "The NPC did not equip Sir Amiel",
    )
    rings.save("MyPreciousesTest_LOTD_Amiel")
    rings.restore("MyPreciousesTest_LOTD_Amiel")
    rings.wait(
        lambda s: (
            assigned(s, amiel, 3)
            and actor(s, npc_id)["rightWorn"] == amiel
            and sum(
                a["actor"] == npc_id and a["source"] == amiel for a in s["assignments"]
            )
            == 1
        ),
        "Sir Amiel did not reload on the player and NPC",
    )
    assert p("Quest", "IsObjectiveCompleted", [17], quest)
    reference = {"form": f"0x{amiel:08X}"}
    p("ObjectReference", "RemoveItem", [reference, 1, True, {"form": npc}], "0x14")
    rings.wait(
        lambda s: not any(a["actor"] == 0x14 for a in s["assignments"]),
        "Transferring Sir Amiel retained the player assignment",
    )
    p("ObjectReference", "RemoveItem", [reference, 3, True, {"form": "0x14"}], npc)
    rings.wait(
        lambda s: not any(a["actor"] == npc_id for a in s["assignments"]),
        "Returning Sir Amiel retained NPC equipment",
    )
    assert p("Actor", "GetWornForm", [64], npc) is None
    assert p("Quest", "IsObjectiveCompleted", [17], quest)
    rings.checkpoint(
        "Sir Amiel survives save/load and bidirectional NPC transfers with quest state intact"
    )
