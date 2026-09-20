import time

import pytest
from bmk.testing import wait_for

from ...support.session import actor, assigned


@pytest.mark.warlock
@pytest.mark.parametrize("target", [0, 6, 9])
def test_warlock_conditional_ward(rings, target):
    p, menu = rings.p, rings.menu
    key = p("Input", "GetMappedKey", ["Left Attack/Block", 0])
    assert 0 <= key < 256, (
        "Map Left Attack/Block to a keyboard key before running the blocking cases"
    )
    ring = rings.client.form_id(0xB2A, "ccbgssse001-fish.esm")
    enchantment = rings.client.form_id(0xC05, "ccbgssse001-fish.esm")
    reference = {"form": f"0x{ring:08X}"}

    def blocking():
        return p("ObjectReference", "GetAnimationVariableBool", ["IsBlocking"], "0x14")

    def ward(expected):
        wait_for(
            lambda: blocking() == expected,
            message="The block control did not reach its expected state",
        )

        def ready(state):
            effects = [e for e in actor(state)["effects"] if e["spell"] == enchantment]
            wards = [e for e in effects if e["effect"] == 0x7DCDB]
            return (
                len(effects) == 3
                and len(wards) == 1
                and not wards[0]["dispelled"]
                and wards[0]["inactive"] == (not expected)
            )

        rings.wait(
            ready,
            "The Warlock enchantment was duplicated or its ward condition did not update",
        )
        wait_for(
            lambda: (
                p("Actor", "GetActorValue", ["WardPower"], "0x14")
                == (25 if expected else 0)
            ),
            message="Warlock ward power did not follow blocking",
        )

    try:
        p("Actor", "UnequipAll", self_form="0x14")
        rings.add(ring)
        rings.add(0x12EB7)
        p("Actor", "EquipItem", [{"form": "0x12EB7"}, False, True], "0x14")
        menu.equip(ring, target)
        menu.close()
        p("Actor", "DrawWeapon", self_form="0x14")
        wait_for(
            lambda: p("Actor", "IsWeaponDrawn", self_form="0x14"),
            message="The blocking sword was not drawn",
        )
        ward(False)
        for _ in range(3):
            with rings.keyboard.hold(key):
                ward(True)
            ward(False)
        menu.open_selector(ring, target)
        menu.ui("Invoke", f"{menu.selector}.EquipSelection")
        rings.wait(
            lambda s: not s["selectorOpen"] and not assigned(s, ring, target),
            "The selector did not unequip the Warlock ring",
        )
        menu.close()
        rings.wait(
            lambda s: not any(e["spell"] == enchantment for e in actor(s)["effects"]),
            "Unequipping through the selector left Warlock enchantments behind",
        )
        menu.equip(ring, target)
        menu.close()
        rings.save("MyPreciousesTest_Warlock")
        rings.restore("MyPreciousesTest_Warlock")
        p("Actor", "DrawWeapon", self_form="0x14")
        wait_for(
            lambda: p("Actor", "IsWeaponDrawn", self_form="0x14"),
            message="Loading lost the blocking weapon",
        )
        ward(False)
        with rings.keyboard.hold(key, max_hold_ms=15000):
            ward(True)
            p("ObjectReference", "RemoveItem", [reference, 1, True], "0x14")
            assert p("ObjectReference", "GetItemCount", [reference], "0x14") == 0
            rings.wait(
                lambda s: (
                    not any(e["spell"] == enchantment for e in actor(s)["effects"])
                ),
                "Removing the ring while blocking left Warlock enchantments behind",
            )
            wait_for(
                lambda: p("Actor", "GetActorValue", ["WardPower"], "0x14") == 0,
                message="The removed ring left ward power behind",
            )
        wait_for(lambda: not blocking(), message="Blocking did not end after removal")
        with rings.keyboard.hold(key):
            wait_for(blocking, message="Blocking did not resume without the ring")
            time.sleep(0.5)
            assert p("Actor", "GetActorValue", ["WardPower"], "0x14") == 0
            assert not any(
                e["spell"] == enchantment for e in actor(rings.state())["effects"]
            )
            rings.checkpoint(
                f"Fishing Warlock ring on finger {target} preserves one conditional ward through repeated blocking, selector unequip and save/load, then clears it when removed while blocking"
            )
    finally:
        rings.keyboard.release_all()
        wait_for(
            lambda: not blocking(),
            message="The block key was not released before cleanup",
        )
