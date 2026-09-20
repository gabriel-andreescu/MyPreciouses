import time

from bmk.testing import wait_for


def test_shared_scripted_sources(rings):
    p = rings.p
    rings.settings(bEnableNpcSupport=1, bEnableLeftRing=1)
    ring = 0x2AC60
    npcs = [rings.npc() for _ in range(3)]

    def add_rings():
        for npc in npcs:
            # Separate additions exercise independent inventory script instances.
            for _ in range(10):
                rings.add(ring, actor_id=npc)
                time.sleep(0.2)

    add_rings()
    sources = rings.shared_npc_sources(npcs, ring, scripted=True)
    rings.save("MyPreciousesTest_SharedSources")
    rings.restore("MyPreciousesTest_SharedSources")
    assert rings.shared_npc_sources(npcs, ring, scripted=True) == sources
    first = int(npcs[0], 16)
    p("ObjectReference", "RemoveItem", [{"form": "0x2AC60"}, 10, True], npcs[0])
    rings.wait(
        lambda s: (
            not any(a["actor"] == first for a in s["assignments"])
            and not any(b["actor"] == first for b in s["scriptBindings"])
        ),
        "Removing one NPC ring set retained its assignments or bindings",
    )
    assert rings.shared_npc_sources(npcs[1:], ring, scripted=True) == sources
    for npc in npcs[1:]:
        p("ObjectReference", "RemoveItem", [{"form": "0x2AC60"}, 10, True], npc)
    rings.wait(
        lambda s: not s["assignments"] and not s["scriptBindings"],
        "Removing all rings left assignments or bindings",
    )
    rings.save("MyPreciousesTest_UnusedSources")
    rings.restore("MyPreciousesTest_UnusedSources")
    add_rings()
    assert rings.shared_npc_sources(npcs, ring, scripted=True) == sources
    rings.checkpoint(
        "Three NPCs share nine ring forms with separate scripts, including equipped and unequipped saves"
    )


def test_shared_enchantments(rings):
    p = rings.p
    rings.settings(bEnableNpcSupport=1, bEnableLeftRing=1)
    ring = 0xFCEFD
    npcs = [rings.npc() for _ in range(3)]
    health = {}
    for npc in npcs:
        health[npc] = p("Actor", "GetActorValueMax", ["Health"], npc)
        rings.add(ring, 10, npc)
    sources = rings.shared_npc_sources(npcs, ring)
    for npc in npcs:
        assert p("Actor", "GetActorValueMax", ["Health"], npc) == health[npc] + 200
    p("ObjectReference", "RemoveItem", [{"form": "0xFCEFD"}, 10, True], npcs[0])
    wait_for(
        lambda: p("Actor", "GetActorValueMax", ["Health"], npcs[0]) == health[npcs[0]],
        message="Removing one NPC rings retained enchantments",
    )
    for npc in npcs[1:]:
        assert p("Actor", "GetActorValueMax", ["Health"], npc) == health[npc] + 200
    rings.add(ring, 10, npcs[0])
    assert rings.shared_npc_sources(npcs, ring) == sources
    rings.settings(iExtraRingMode=1)
    cosmetic = rings.shared_npc_sources(npcs, ring)
    assert set(cosmetic).isdisjoint(sources), (
        "Cosmetic mode modified shared functional forms"
    )
    for npc in npcs:
        assert p("Actor", "GetActorValueMax", ["Health"], npc) == health[npc] + 20
    rings.settings(iExtraRingMode=0)
    assert rings.shared_npc_sources(npcs, ring) == sources
    rings.save("MyPreciousesTest_SharedEnchantments")
    rings.restore("MyPreciousesTest_SharedEnchantments")
    assert rings.shared_npc_sources(npcs, ring) == sources
    for npc in npcs:
        assert p("Actor", "GetActorValueMax", ["Health"], npc) == health[npc] + 200
    rings.checkpoint(
        "Shared ring forms preserve independent NPC bonuses through removal, cosmetic mode and save/load"
    )
