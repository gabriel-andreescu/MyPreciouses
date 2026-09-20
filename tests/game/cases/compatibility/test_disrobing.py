import pytest
from bmk.testing import wait_for

from ...support.session import actor, assigned


@pytest.mark.compatibility("SaintsAndSeducers")
def test_disrobing_ring_torso_conflicts_and_models(rings):
    p, menu = rings.p, rings.menu
    p("Actor", "UnequipAll", self_form="0x14")
    ring = rings.client.form_id(0x183E63, "ccBGSSSE025-AdvDSGS.esm")
    armor = {"form": "0x12E49"}
    for form in (ring, 0x877C9, 0x12E49):
        rings.add(form)
    p("Actor", "EquipItem", [armor, False, True], "0x14")
    p("Game", "ForceFirstPerson")
    p("Actor", "DrawWeapon", self_form="0x14")
    menu.equip(0x877C9, 0)
    menu.close()

    def visual(state):
        return next(v for v in actor(state)["visuals"] if v["target"] == 0)

    control = rings.wait(
        lambda s: (
            visual(s)["thirdPersonGeometry"] > 0
            and visual(s)["firstPersonGeometry"] > 0
        ),
        "The gold diamond ring control did not attach both models",
    )
    expected = visual(control)
    menu.equip(ring, 0)
    menu.close()
    for load in (False, True):
        if load:
            rings.save("MyPreciousesTest_Disrobing")
            rings.restore("MyPreciousesTest_Disrobing")
            p("Game", "ForceFirstPerson")
            p("Actor", "DrawWeapon", self_form="0x14")
        rings.wait(
            lambda s: (
                assigned(s, ring, 0)
                and visual(s)["thirdPersonGeometry"] == expected["thirdPersonGeometry"]
                and visual(s)["firstPersonGeometry"] == expected["firstPersonGeometry"]
            ),
            "The virtual Disrobing ring lost its ring model or attached additional body geometry",
        )
        assert p("Actor", "IsEquipped", [armor], "0x14")
    rings.checkpoint(
        "Virtual Ring of Disrobing preserves torso armor and attaches only ring geometry in both views through save/load"
    )
    menu.equip(ring, 6)
    menu.close()
    wait_for(
        lambda: not p("Actor", "IsEquipped", [armor], "0x14"),
        message="Native Ring of Disrobing no longer occupies the torso slot",
    )
    p("Actor", "EquipItem", [armor, False, True], "0x14")
    rings.wait(
        lambda s: actor(s)["rightWorn"] == 0 and not s["assignments"],
        "Torso armor did not displace the native hybrid-slot ring",
    )
    menu.equip(ring, 0)
    menu.close()
    p("Actor", "UnequipAll", self_form="0x14")
    rings.wait(
        lambda s: (
            not s["assignments"]
            and not any(v["thirdPersonGeometry"] > 0 for v in actor(s)["visuals"])
        ),
        "Unequipping the Disrobing ring retained attached geometry",
    )
    rings.checkpoint(
        "Native Ring of Disrobing retains torso-slot conflicts and cleans up when replaced or unequipped"
    )
