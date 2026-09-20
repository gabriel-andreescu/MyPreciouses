import time

import pytest
from bmk.testing import wait_for

from ...support.session import assigned

pytestmark = pytest.mark.compatibility("SandsOfTime")


@pytest.mark.parametrize("target", [3, 6])
def test_escape_ring_consumption(rings, target):
    p, menu = rings.p, rings.menu
    ring = rings.client.form_id(0x1A1B, "SOTFull.esp")
    word = {"form": f"0x{rings.client.form_id(0x1A1C, 'SOTFull.esp'):08X}"}
    reference = {"form": f"0x{ring:08X}"}
    rings.settings(bEnableNpcSupport=1)
    assert not p("Game", "IsWordUnlocked", [word]), (
        "The SOT baseline already unlocked Wail of Pain"
    )
    rings.add(ring)
    menu.equip(ring, target)
    menu.close()
    wait_for(
        lambda: p("Game", "IsWordUnlocked", [word]),
        message="The Ring of Escape did not unlock its word",
    )
    assert assigned(rings.state(), ring, target)
    p("Actor", "UnequipAll", self_form="0x14")
    wait_for(
        lambda: p("ObjectReference", "GetItemCount", [reference], "0x14") == 0,
        message="The Ring of Escape was not consumed on removal",
    )
    rings.wait(
        lambda s: (
            not any(a["source"] == ring for a in s["assignments"])
            and not any(b["source"] == ring for b in s["scriptBindings"])
        ),
        "The consumed Ring of Escape retained an assignment or script binding",
    )
    rings.checkpoint(
        f"SOT Ring of Escape unlocks its word and is consumed after removal from target {target}"
    )


def test_npc_escape_ring_scripts(rings):
    p = rings.p
    ring = rings.client.form_id(0x1A1B, "SOTFull.esp")
    reference = {"form": f"0x{ring:08X}"}
    rings.settings(bEnableNpcSupport=1)
    npc = rings.npc()
    npc_id = int(npc, 16)
    # Separate additions create distinct inventory script instances for every copy.
    for _ in range(10):
        rings.add(ring, actor_id=npc)
        time.sleep(0.2)
    rings.wait(
        lambda s: (
            sum(a["actor"] == npc_id and a["source"] == ring for a in s["assignments"])
            == 9
            and sum(
                b["source"] == ring and not b["suspended"] for b in s["scriptBindings"]
            )
            == 9
        ),
        "The NPC did not exercise all nine virtual Ring of Escape script bindings",
    )
    rings.save("MyPreciousesTest_SOT_Npc")
    rings.restore("MyPreciousesTest_SOT_Npc")
    assert p("ObjectReference", "GetItemCount", [reference], npc) == 10
    p("ObjectReference", "RemoveItem", [reference, 10, True], npc)
    rings.wait(
        lambda s: (
            not any(a["source"] == ring for a in s["assignments"])
            and not any(b["source"] == ring for b in s["scriptBindings"])
        ),
        "Removing the NPC rings retained assignments or script bindings",
    )
    rings.checkpoint(
        "SOT Ring of Escape NPC copies survive save/load and release their virtual script bindings on removal"
    )
