import time

import pytest

from ...support.session import actor


@pytest.mark.parametrize(
    "ring", [0xF82FE, 0x3B97C], ids=["cursed-hircine", "script-locked-silver"]
)
def test_protected_ring(rings, ring):
    p, menu = rings.p, rings.menu
    p("Actor", "UnequipAll", self_form="0x14")
    rings.add(ring)
    rings.add(0x1CF2B)
    reference = {"form": f"0x{ring:08X}"}
    # Sinding's dialogue uses this native protection when giving the cursed ring.
    p("Actor", "EquipItem", [reference, True, True], "0x14")
    rings.wait(
        lambda s: actor(s)["rightWorn"] == ring, "The protected ring did not equip"
    )
    for load in (False, True):
        if load:
            rings.save("MyPreciousesTest_ProtectedRing")
            rings.restore("MyPreciousesTest_ProtectedRing")
        menu.select(ring)
        menu.ui("InvokeInt", "_root.Menu_mc.SetPlatform", 1)
        for hand in (0, 1):
            menu.ui("InvokeIntA", "_root.Menu_mc.AttemptEquip", [hand, 0])
            time.sleep(0.25)
            state = rings.state()
            assert (
                not state["selectorOpen"]
                and actor(state)["rightWorn"] == ring
                and not state["assignments"]
            )
        menu.open_selector(0x1CF2B, 6)
        menu.ui("Invoke", f"{menu.selector}.EquipSelection")
        time.sleep(0.25)
        state = rings.state()
        assert actor(state)["rightWorn"] == ring and not state["assignments"], (
            "Another ring displaced a protected native ring"
        )
        menu.close()
    menu.equip(0x1CF2B, 0)
    menu.close()
    assert actor(rings.state())["rightWorn"] == ring
    p("Actor", "UnequipItem", [reference, False, True], "0x14")
    rings.wait(
        lambda s: actor(s)["rightWorn"] == 0,
        "The script could not release its ring lock",
    )
    menu.equip(ring, 1)
    menu.close()
    rings.checkpoint(
        f"Protected ring 0x{ring:08X} blocks removal and replacement through save/load and allows script release"
    )
