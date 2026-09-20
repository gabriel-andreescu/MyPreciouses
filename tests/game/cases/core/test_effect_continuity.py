import time
from contextlib import contextmanager

from bmk.testing import wait_for

from ...support.session import actor, assigned


def test_effect_continuity(rings):
    p, menu = rings.p, rings.menu
    p("Actor", "UnequipAll", self_form="0x14")
    rings.settings(bEnableNpcSupport=1)
    ring = 0xFCEFD
    enchantment = p("Armor", "GetEnchantment", self_form="0xFCEFD")["formId"]
    base_health = p("Actor", "GetActorValueMax", ["Health"], "0x14")
    rings.add(ring)
    menu.equip(ring, 0)
    menu.close()

    def age(actor_id="0x14", count=1):
        effects = rings.call("inspect", {"kind": "effects", "formId": actor_id})[
            "activeEffects"
        ]
        effects = [e for e in effects if e["spell"]["formId"] == enchantment]
        assert len(effects) == count, (
            f"Expected {count} health-ring effects on {actor_id}, found {len(effects)}"
        )
        return min(e["elapsed"] for e in effects)

    @contextmanager
    def continuous(actor_id="0x14", count=1):
        before = age(actor_id, count)
        assert before >= 15, "The effect must age before checking for a restart"
        start = time.monotonic()
        yield
        after = age(actor_id, count)
        # A restarted effect cannot regain its previous age within this interval.
        assert time.monotonic() - start < before, (
            "The action took too long to distinguish preservation from a restart"
        )
        assert after >= before, (
            f"An unrelated action restarted a ring effect on {actor_id} ({before} to {after} seconds)"
        )

    wait_for(
        lambda: age() >= 15,
        message="The virtual ring effect did not accumulate elapsed time",
        timeout=30,
    )
    rings.add(0x12EB7)
    with continuous():
        menu.select(0x12EB7)
        menu.ui("InvokeInt", "_root.Menu_mc.SetPlatform", 1)
        menu.ui("InvokeIntA", "_root.Menu_mc.AttemptEquip", [0, 0])
        menu.close()
        assert (
            p("Actor", "GetEquippedWeapon", [False], "0x14")["formId"] == "0x00012EB7"
        )
    with continuous():
        p("Actor", "UnequipItem", [{"form": "0x12EB7"}, False, True], "0x14")
        assert p("Actor", "GetEquippedWeapon", [False], "0x14") is None
    for item in (0x64B2E, 0x3EADD):
        rings.add(item)
        reference = {"form": f"0x{item:08X}"}
        before = p("ObjectReference", "GetItemCount", [reference], "0x14")
        with continuous():
            menu.select(item)
            menu.ui("InvokeInt", "_root.Menu_mc.SetPlatform", 1)
            menu.ui("InvokeIntA", "_root.Menu_mc.AttemptEquip", [0, 0])
            menu.close()
            assert (
                p("ObjectReference", "GetItemCount", [reference], "0x14") == before - 1
            )
    with continuous():
        rings.save("MyPreciousesTest_EffectContinuity")
        rings.restore("MyPreciousesTest_EffectContinuity")
    assert assigned(rings.state(), ring, 0)
    assert p("Actor", "GetActorValueMax", ["Health"], "0x14") == base_health + 20
    rings.checkpoint(
        "Virtual ring effects retain their age through weapon changes, food, potions and save/load"
    )
    npc = rings.npc()
    npc_id = int(npc, 16)
    npc_health = p("Actor", "GetActorValueMax", ["Health"], npc)
    rings.add(0xFCEFE, actor_id=npc)
    rings.add(ring, actor_id=npc)
    rings.wait(
        lambda s: (
            sum(a["actor"] == npc_id for a in s["assignments"]) == 1
            and actor(s, npc_id)["rightWorn"] == 0xFCEFE
        ),
        "The NPC did not equip both health rings",
    )
    target = next(
        a["target"]
        for a in rings.state()["assignments"]
        if a["actor"] == npc_id and a["source"] == ring
    )
    wait_for(
        lambda: age(npc) >= 15,
        message="The NPC ring effect did not accumulate elapsed time",
        timeout=30,
    )
    with continuous(npc):
        rings.add(0x3B97C, actor_id=npc)
        rings.wait(
            lambda s: (
                sum(a["actor"] == npc_id for a in s["assignments"]) == 2
                and assigned(s, ring, target, npc_id)
            ),
            "The NPC did not equip the additional plain ring",
        )
    with continuous(npc):
        p("ObjectReference", "RemoveItem", [{"form": "0x3B97C"}, 1, True], npc)
        rings.wait(
            lambda s: (
                sum(a["actor"] == npc_id for a in s["assignments"]) == 1
                and assigned(s, ring, target, npc_id)
            ),
            "The NPC retained the removed plain ring",
        )
    assert p("Actor", "GetActorValueMax", ["Health"], npc) == npc_health + 50
    with continuous(npc):
        rings.save("MyPreciousesTest_NpcEffectContinuity")
        rings.restore("MyPreciousesTest_NpcEffectContinuity")
    assert assigned(rings.state(), ring, target, npc_id)
    assert p("Actor", "GetActorValueMax", ["Health"], npc) == npc_health + 50
    rings.checkpoint(
        "NPC auto-equip refreshes and save/load preserve existing virtual effects"
    )
