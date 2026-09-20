import time

import pytest
from bmk.testing import wait_for

from ...support.session import assigned


@pytest.fixture(params=["Namira", "Necromancy"])
def scripted_ring(request, rings):
    if request.param == "Namira":
        return request.param, 0x2C37B, {"form": "0x000EE5C3"}
    form = rings.client.form_id
    return (
        request.param,
        form(0x1DB9A, "Dragonborn.esm"),
        {"form": f"0x{form(0x27328, 'Dragonborn.esm'):08X}"},
    )


def test_script_state_and_cosmetic_mode(rings):
    p, menu = rings.p, rings.menu
    p("Actor", "UnequipAll", self_form="0x14")
    perk = {"form": "0xEE5C3"}

    def has_perk():
        return p("Actor", "HasPerk", [perk], "0x14")

    assert not has_perk(), "The baseline must not have the Namira cannibalism perk"
    rings.add(0x2C37B)
    menu.equip(0x2C37B, 0)
    menu.close()
    wait_for(
        has_perk, message="Namira did not run its equip script on a virtual finger"
    )
    # A script-owned state change must survive an enchantment refresh and load.
    p("Actor", "RemovePerk", [perk], "0x14")
    rings.settings(iEnchantmentStrengthMode=1)
    assert not has_perk(), "Refreshing enchantments replayed the ring equip script"
    rings.save("MyPreciousesTest_ScriptState")
    rings.restore("MyPreciousesTest_ScriptState")
    assert assigned(rings.state(), 0x2C37B, 0)
    assert not has_perk(), "Loading replayed the ring equip script"
    rings.checkpoint(
        "Script-owned state survives effect refresh and save/load without replaying equip"
    )
    rings.settings(iExtraRingMode=1)
    rings.settings(iExtraRingMode=0)
    wait_for(
        has_perk, message="Returning to functional mode did not resume the ring script"
    )
    rings.settings(iExtraRingMode=1)
    wait_for(lambda: not has_perk(), message="Cosmetic mode retained the scripted perk")
    rings.settings(iExtraRingMode=0)
    wait_for(has_perk, message="The ring script did not resume after cosmetic mode")
    p("Actor", "UnequipAll", self_form="0x14")
    wait_for(
        lambda: not has_perk(), message="Unequipping Namira retained its scripted perk"
    )
    rings.checkpoint("Scripted perks suspend, resume and clear with virtual equipment")


def test_native_virtual_script_transfers(rings, scripted_ring):
    name, ring, perk = scripted_ring
    p, menu = rings.p, rings.menu
    p("Actor", "UnequipAll", self_form="0x14")
    rings.add(ring)
    for target in (0, 6, 0, 6, 0):
        menu.equip(ring, target)
        menu.close()
        # Both native inventory events and virtual equipment events must finish.
        time.sleep(2)
        assert p("Actor", "HasPerk", [perk], "0x14"), (
            f"{name} lost its perk on target {target}"
        )
    save = f"MyPreciousesTest_{name}Transfers"
    rings.save(save)
    rings.restore(save)
    assert p("Actor", "HasPerk", [perk], "0x14"), f"{name} lost its perk after loading"
    p("Actor", "UnequipAll", self_form="0x14")
    wait_for(
        lambda: not p("Actor", "HasPerk", [perk], "0x14"),
        message=f"{name} retained its perk after unequipping",
    )
    assert not rings.state()["scriptBindings"]
    rings.checkpoint(
        f"{name} native and virtual transfers retain scripts through save/load and clean up on unequip"
    )


def test_npc_script_cell_reload_and_return(rings, scripted_ring):
    name, ring, perk = scripted_ring
    p, menu = rings.p, rings.menu
    rings.settings(bEnableNpcSupport=1)
    reference = {"form": f"0x{ring:08X}"}
    npc = rings.npc(persistent=True)
    p("Actor", "SetPlayerTeammate", [False, False], npc)
    npc_id = int(npc, 16)
    for _ in range(10):
        rings.add(ring, actor_id=npc)
        time.sleep(0.2)
    sources = rings.shared_npc_sources([npc], ring, scripted=True)
    rings.reload_npc_cell(npc)
    assert rings.shared_npc_sources([npc], ring, scripted=True) == sources
    rings.save("MyPreciousesTest_NpcScriptCell")
    rings.restore("MyPreciousesTest_NpcScriptCell")
    p("ObjectReference", "RemoveItem", [reference, 10, True, {"form": "0x14"}], npc)
    wait_for(
        lambda: p("ObjectReference", "GetItemCount", [reference], "0x14") == 10,
        message="The NPC did not return the scripted ring",
    )
    rings.wait(
        lambda s: not any(b["actor"] == npc_id for b in s["scriptBindings"]),
        "Transferring the scripted ring retained its NPC binding",
    )
    menu.equip(ring, 0)
    menu.close()
    wait_for(
        lambda: p("Actor", "HasPerk", [perk], "0x14"),
        message=f"The returned {name} ring lost its equip script",
    )
    p("Actor", "UnequipAll", self_form="0x14")
    wait_for(
        lambda: not p("Actor", "HasPerk", [perk], "0x14"),
        message=f"The returned {name} ring lost its unequip script",
    )
    rings.checkpoint(
        f"{name} inventory scripts survive NPC cell unload, save/load and transfer back to the player"
    )


@pytest.mark.parametrize("removal", ["Give", "Drop", "Destroy"])
def test_scripted_ring_inventory_removal(rings, scripted_ring, removal):
    name, ring, perk = scripted_ring
    p, menu = rings.p, rings.menu
    rings.settings(bEnableNpcSupport=0)
    reference = {"form": f"0x{ring:08X}"}
    rings.add(ring)
    menu.equip(ring, 0)
    menu.close()
    wait_for(
        lambda: p("Actor", "HasPerk", [perk], "0x14"),
        message=f"{name} did not grant its perk",
    )
    if removal == "Give":
        receiver = rings.npc()
        p(
            "ObjectReference",
            "RemoveItem",
            [reference, 1, True, {"form": receiver}],
            "0x14",
        )
    elif removal == "Drop":
        p("ObjectReference", "DropObject", [reference, 1], "0x14")
    else:
        p("ObjectReference", "RemoveItem", [reference, 1, True], "0x14")
    wait_for(
        lambda: p("ObjectReference", "GetItemCount", [reference], "0x14") == 0,
        message=f"{removal} did not remove {name}",
    )
    wait_for(
        lambda: not p("Actor", "HasPerk", [perk], "0x14"),
        message=f"{removal} retained {name}'s perk",
    )
    rings.wait(
        lambda s: not s["assignments"] and not s["scriptBindings"],
        f"{removal} retained virtual equipment",
    )
    if removal == "Drop":
        refs = rings.call("inspect", {"kind": "refs", "radius": 400, "limit": 200})
        dropped = [r for r in refs["refs"] if r["base"]["formId"] == reference["form"]]
        assert len(dropped) == 1, f"Expected one dropped {name} ring"
        p(
            "ObjectReference",
            "Activate",
            [{"form": "0x14"}, False],
            dropped[0]["formId"],
        )
        wait_for(
            lambda: p("ObjectReference", "GetItemCount", [reference], "0x14") == 1,
            message=f"Could not pick up {name}",
        )
        menu.equip(ring, 0)
        menu.close()
        wait_for(
            lambda: p("Actor", "HasPerk", [perk], "0x14"),
            message=f"Picking up {name} lost its script",
        )
        p("Actor", "UnequipAll", self_form="0x14")
        wait_for(
            lambda: not p("Actor", "HasPerk", [perk], "0x14"),
            message=f"The recovered {name} ring retained its perk after unequipping",
        )
    rings.checkpoint(f"{name} {removal} clears scripts and equipment")
