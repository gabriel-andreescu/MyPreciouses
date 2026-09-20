import time

import pytest
from bmk.testing import wait_for

from ...support.session import assigned


@pytest.mark.parametrize("mode", ["Empty", "Native", "Virtual", "Cosmetic"])
def test_necromancy_reanimation_and_hit(rings, mode):
    p, menu = rings.p, rings.menu
    p("Actor", "UnequipAll", self_form="0x14")
    form = rings.client.form_id
    ring = form(0x1DB9A, "Dragonborn.esm")
    ability = {"form": f"0x{form(0x27329, 'Dragonborn.esm'):08X}"}
    perk = {"form": f"0x{form(0x27328, 'Dragonborn.esm'):08X}"}
    if mode != "Empty":
        rings.add(ring)
        target = 6 if mode == "Native" else 0
        menu.equip(ring, target)
        menu.close()
        rings.wait(
            lambda s: assigned(s, ring, target), "Necromancy ring is not equipped"
        )
        wait_for(
            lambda: p("Actor", "HasPerk", [perk], "0x14"),
            message="Necromancy perk was not granted",
        )
    if mode == "Cosmetic":
        rings.settings(iExtraRingMode=1)
        wait_for(
            lambda: not p("Actor", "HasPerk", [perk], "0x14"),
            message="Cosmetic mode retained Necromancy perk",
        )
    for axis, value in (("x", -400), ("y", 1700), ("z", 6980)):
        rings.call("console", {"command": f"player.setpos {axis} {value}"})
    npc = p(
        "ObjectReference", "PlaceAtMe", [{"form": "0xC3CA0"}, 1, False, False], "0x14"
    )["formId"]
    p("Actor", "SetRestrained", [True], npc)
    p("ObjectReference", "MoveTo", [{"form": "0x14"}, 0.0, 250.0, 0.0, False], npc)
    time.sleep(0.5)
    p("Actor", "DamageActorValue", ["Health", 1000.0], npc)
    time.sleep(3)
    p("ObjectReference", "MoveTo", [{"form": npc}, 0.0, -250.0, 0.0, False], "0x14")
    p("Game", "ForceFirstPerson")
    p("Actor", "AddSpell", [{"form": "0x7E8E1"}, False], "0x14")
    p("Actor", "EquipSpell", [{"form": "0x7E8E1"}, 0], "0x14")
    p("Actor", "DrawWeapon", self_form="0x14")
    rings.call("console", {"command": "player.setangle z 8"})
    rings.call("console", {"command": "player.setangle x 20"})
    time.sleep(1)
    p("PO3_SKSEFunctions", "LaunchSpell", [{"form": "0x14"}, {"form": "0x7E8E1"}, 0])
    wait_for(
        lambda: not p("Actor", "IsDead", self_form=npc),
        message=f"{mode}: reanimation failed",
        timeout=15,
    )
    time.sleep(6)
    granted = p("Actor", "HasSpell", [ability], npc)
    before = p("Actor", "GetActorValue", ["Health"], npc)
    p("Actor", "UnequipSpell", [{"form": "0x7E8E1"}, 0], "0x14")
    rings.add(0x1397E)
    p("Actor", "EquipItem", [{"form": "0x1397E"}, False, True], "0x14")
    p("Actor", "DrawWeapon", self_form="0x14")
    p("ObjectReference", "MoveTo", [{"form": npc}, 0.0, -80.0, 0.0, False], "0x14")
    rings.call("console", {"command": "player.setangle z 0"})
    rings.call("console", {"command": "player.setangle x 0"})
    time.sleep(2)
    p("Debug", "SendAnimationEvent", [{"form": "0x14"}, "attackStart"])
    time.sleep(2)
    after = p("Actor", "GetActorValue", ["Health"], npc)
    dead = p("Actor", "IsDead", self_form=npc)
    functional = mode in ("Native", "Virtual")
    assert granted == functional, (
        f"{mode}: incorrect Necromancy ability on the reanimated actor"
    )
    if functional:
        assert dead and after < -9000, (
            f"{mode}: the hit did not trigger Necromancy's explosion"
        )
    else:
        assert not dead and 0 < after < before, (
            f"{mode}: the control must survive a verified hit"
        )
    rings.checkpoint(f"Necromancy {mode} reanimation and hit response")
