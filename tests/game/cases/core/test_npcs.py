import pytest

from ...support.session import actor, assigned


@pytest.mark.parametrize(
    "mode,name,bonus",
    [(0, "Full", 200), (1, "Fixed", 100), (2, "Split", 20)],
    ids=["full", "fixed", "split"],
)
def test_npc_initialization_and_cell_reload(rings, mode, name, bonus):
    p = rings.p
    rings.settings(
        bEnableNpcSupport=1,
        iEnchantmentStrengthMode=mode,
        iFixedEnchantmentStrengthPercent=50,
    )
    npc = rings.npc(disabled=True, persistent=True)
    p("Actor", "SetPlayerTeammate", [False, False], npc)
    npc_id = int(npc, 16)
    health = p("Actor", "GetActorValueMax", ["Health"], npc)
    assert p("ObjectReference", "IsDisabled", self_form=npc)
    rings.add(0xFCEFD, 10, npc)
    p("ObjectReference", "Enable", [False], npc)

    def ready(state, *, visuals=False, native=False):
        current = actor(state, npc_id)
        return (
            current is not None
            and sum(a["actor"] == npc_id for a in state["assignments"]) == 9
            and sum(e["spell"] == 0x49509 for e in current["effects"]) == 10
            and (
                not visuals
                or sum(v["thirdPersonGeometry"] > 0 for v in current["visuals"]) == 10
            )
            and (not native or current["rightWorn"] == 0xFCEFD)
        )

    def sources(state):
        return sorted(
            e["source"]
            for e in actor(state, npc_id)["effects"]
            if e["spell"] == 0x49509
        )

    state = rings.wait(
        lambda s: ready(s, visuals=True, native=True),
        f"Enabling an NPC did not apply ten rings in {name} mode",
    )
    assert p("Actor", "GetActorValueMax", ["Health"], npc) == health + bonus
    original = sources(state)
    assert len(set(original)) == 10
    rings.save("MyPreciousesTest_NpcEffects")
    rings.restore("MyPreciousesTest_NpcEffects")
    state = rings.wait(
        ready, f"Loading lost or duplicated NPC ring enchantments in {name} mode"
    )
    assert sources(state) == original
    assert p("Actor", "GetActorValueMax", ["Health"], npc) == health + bonus
    rings.reload_npc_cell(npc)
    state = rings.wait(
        lambda s: ready(s, visuals=True),
        f"Returning to the cell lost NPC rings in {name} mode",
    )
    assert sources(state) == original
    assert p("Actor", "GetActorValueMax", ["Health"], npc) == health + bonus
    rings.checkpoint(
        f"NPC initialization, save/load and cell unload/reload preserve ten independent ring effects in {name} mode"
    )


def test_npc_priorities_support_toggle_and_transfer(rings):
    p = rings.p
    rings.settings(bEnableNpcSupport=1, bEnableLeftRing=1)
    npc = rings.npc()
    npc_id = int(npc, 16)
    health = p("Actor", "GetActorValueMax", ["Health"], npc)
    rings.add(0xFCEFD, 10, npc)
    rings.wait(
        lambda s: (
            sum(a["actor"] == npc_id for a in s["assignments"]) == 9
            and actor(s, npc_id)["rightWorn"] == 0xFCEFD
            and sum(v["thirdPersonGeometry"] > 0 for v in actor(s, npc_id)["visuals"])
            == 10
        ),
        "The NPC did not equip ten rings and attach their models",
    )
    assert p("Actor", "GetActorValueMax", ["Health"], npc) == health + 200
    rings.add(0xFCEFE, actor_id=npc)
    rings.wait(
        lambda s: actor(s, npc_id)["rightWorn"] == 0xFCEFE,
        "The NPC did not prioritize the more valuable ring",
    )
    assert p("Actor", "GetActorValueMax", ["Health"], npc) == health + 210
    p(
        "ObjectReference",
        "RemoveItem",
        [{"form": "0xFCEFE"}, 1, True, {"form": "0x14"}],
        npc,
    )
    rings.wait(
        lambda s: actor(s, npc_id)["rightWorn"] == 0xFCEFD,
        "The NPC did not fill the slot after a transfer",
    )
    assert p("Actor", "GetActorValueMax", ["Health"], npc) == health + 200
    assert p("ObjectReference", "GetItemCount", [{"form": "0xFCEFE"}], "0x14") == 1
    rings.checkpoint(
        "NPC equipment prioritizes value, uses all fingers and refills a transferred slot"
    )
    rings.settings(bEnableNpcSupport=0)
    rings.wait(
        lambda s: not any(a["actor"] == npc_id for a in s["assignments"]),
        "Disabling NPC support retained virtual assignments",
    )
    assert p("Actor", "GetActorValueMax", ["Health"], npc) == health + 20
    rings.settings(bEnableNpcSupport=1)
    rings.wait(
        lambda s: sum(a["actor"] == npc_id for a in s["assignments"]) == 9,
        "Enabling NPC support did not restore equipment",
    )
    assert p("Actor", "GetActorValueMax", ["Health"], npc) == health + 200
    p(
        "ObjectReference",
        "RemoveItem",
        [{"form": "0xFCEFD"}, 9, True, {"form": "0x14"}],
        npc,
    )
    rings.wait(
        lambda s: not any(a["actor"] == npc_id for a in s["assignments"]),
        "Transferring virtual copies retained NPC assignments",
    )
    assert p("Actor", "GetActorValueMax", ["Health"], npc) == health + 20
    assert p("ObjectReference", "GetItemCount", [{"form": "0xFCEFD"}], "0x14") == 9
    rings.checkpoint(
        "NPC support toggles and transfers clean up virtual effects without duplicating items"
    )


def test_npc_matrimony_disabled_finger_exception(rings):
    p = rings.p
    slots = {
        f"bEnable{hand}{finger}": 0
        for hand in ("Left", "Right")
        for finger in ("Thumb", "Middle", "Ring", "Pinky")
    }
    rings.settings(
        **slots,
        bEnableLeftIndex=1,
        bEnableNpcSupport=1,
        bNpcAlwaysEquipBondOfMatrimonyOnLeftRingFinger=1,
    )
    npc = rings.npc()
    npc_id = int(npc, 16)
    for ring in (0x3B97C, 0xFCEFD, 0xC5809):
        rings.add(ring, actor_id=npc)

    def ready(state):
        items = [a for a in state["assignments"] if a["actor"] == npc_id]
        current = actor(state, npc_id)
        return (
            assigned(state, 0xC5809, 3, npc_id)
            and len(items) == 2
            and sum(
                a["target"] == 1 and a["source"] in (0x3B97C, 0xFCEFD) for a in items
            )
            == 1
            and current["rightWorn"] in (0x3B97C, 0xFCEFD)
            and sum(v["thirdPersonGeometry"] > 0 for v in current["visuals"]) == 3
        )

    state = rings.wait(
        ready,
        "The NPC did not wear two ordinary rings plus Matrimony on its disabled special finger",
    )
    before = [a for a in state["assignments"] if a["actor"] == npc_id]
    rings.save("MyPreciousesTest_NpcMatrimony")
    rings.restore("MyPreciousesTest_NpcMatrimony")
    assert [a for a in rings.state()["assignments"] if a["actor"] == npc_id] == before
    p("ObjectReference", "RemoveItem", [{"form": "0xC5809"}, 1, True], npc)
    rings.wait(
        lambda s: (
            [a["target"] for a in s["assignments"] if a["actor"] == npc_id] == [1]
        ),
        "Removing Matrimony made the disabled finger available to ordinary rings",
    )
    rings.checkpoint(
        "NPCs retain two ordinary rings plus Matrimony on its disabled special finger through save/load and removal"
    )
