import time

import pytest
from bmk.testing import wait_for

from ...support.session import actor


@pytest.mark.compatibility("ValSerano")
@pytest.mark.parametrize("outfit", ["default", "sleeping"])
def test_val_outfit_ring_copies(rings, outfit):
    p = rings.p
    form = rings.client.form_id
    base, ring, default, sleeping = [
        form(i, "AX ValSerano.esp") for i in (0xAA0F, 0x7A061, 0x23F3E, 0x2EE9A5)
    ]
    reference = {"form": f"0x{ring:08X}"}
    rings.settings(bEnableNpcSupport=1)
    npc = rings.npc(base_id=base, persistent=True)
    npc_id = int(npc, 16)
    wait_for(
        lambda: p("ObjectReference", "Is3DLoaded", self_form=npc),
        message="Val did not load",
    )
    if outfit == "sleeping":
        p("Actor", "SetOutfit", [{"form": f"0x{default:08X}"}, True], npc)
        p("Actor", "SetOutfit", [{"form": f"0x{sleeping:08X}"}, False], npc)
    time.sleep(1)
    assert p("ObjectReference", "GetItemCount", [reference], npc) == 2
    inventory = rings.call("inspect", {"kind": "inventory", "formId": npc})["items"]
    assert (
        sum(i["formId"] == reference["form"] and i["equipped"] for i in inventory) == 1
    )
    assert not any(a["actor"] == npc_id for a in rings.state()["assignments"])
    rings.add(ring, actor_id=npc)
    state = rings.wait(
        lambda s: (
            sum(a["actor"] == npc_id and a["source"] == ring for a in s["assignments"])
            == 1
            and sum(e["spell"] == 0x92A76 for e in actor(s, npc_id)["effects"]) == 2
            and sum(v["thirdPersonGeometry"] > 0 for v in actor(s, npc_id)["visuals"])
            == 2
        ),
        f"The separately acquired ring did not equip with Val's {outfit} outfit",
    )
    source = next(
        a["effectSource"] for a in state["assignments"] if a["actor"] == npc_id
    )
    rings.save("MyPreciousesTest_ValOutfit")
    rings.restore("MyPreciousesTest_ValOutfit")
    rings.wait(
        lambda s: (
            sum(
                a["actor"] == npc_id and a["effectSource"] == source
                for a in s["assignments"]
            )
            == 1
            and sum(e["spell"] == 0x92A76 for e in actor(s, npc_id)["effects"]) == 2
        ),
        "Loading changed the active and inactive outfit ring assignments",
    )
    assert p("ObjectReference", "GetItemCount", [reference], npc) == 3
    p("ObjectReference", "RemoveItem", [reference, 1, True], npc)
    rings.wait(
        lambda s: not any(a["actor"] == npc_id for a in s["assignments"]),
        "Removing the additional ring left a virtual outfit copy equipped",
    )
    assert p("ObjectReference", "GetItemCount", [reference], npc) == 2
    effects = rings.call("inspect", {"kind": "effects", "formId": npc})["activeEffects"]
    assert sum(e["spell"]["formId"] == "0x00092A76" for e in effects) == 1
    rings.checkpoint(
        f"Val's {outfit} outfit reserves the inactive ring while an acquired copy equips, survives save/load and can be removed"
    )
