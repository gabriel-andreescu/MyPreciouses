import json
from pathlib import Path

import pytest
from bmk.testing import wait_for


@pytest.mark.restart
def test_effect_sources_after_process_restart(rings, request):
    phase = request.config.getoption("restart_phase")
    path = Path(request.config.getoption("game_results")) / "restart-state.json"
    p, menu = rings.p, rings.menu
    empty_save = "MyPreciousesTest_UnusedEffectSources"
    custom_save = "MyPreciousesTest_CustomEffectSources"
    rings.settings(bEnableNpcSupport=1, bEnableLeftRing=1)
    if phase == "prepare":
        npcs = [rings.npc() for _ in range(3)]
        health = {}
        for npc in npcs:
            health[npc] = p("Actor", "GetActorValueMax", ["Health"], npc)
            rings.add(0xFCEFD, 10, npc)
        sources = rings.shared_npc_sources(npcs, 0xFCEFD)
        for npc in npcs:
            wait_for(
                lambda npc=npc: (
                    p("Actor", "GetActorValueMax", ["Health"], npc) == health[npc] + 200
                ),
                message="NPC ring bonuses did not apply",
            )
            p("ObjectReference", "RemoveItem", [{"form": "0xFCEFD"}, 10, True], npc)
            wait_for(
                lambda npc=npc: (
                    p("Actor", "GetActorValueMax", ["Health"], npc) == health[npc]
                ),
                message="Removing NPC rings retained bonuses",
            )
        rings.wait(
            lambda s: not s["assignments"],
            "The restart checkpoint retained equipped rings",
        )
        rings.save(empty_save)
        rings.restore()
        player_health = p("Actor", "GetActorValueMax", ["Health"], "0x14")
        silver_name = p("Form", "GetName", self_form="0x3B97C")
        names = ["MyPreciouses Restart Custom Ring", silver_name]
        enchantments = []
        for index in range(2):
            p("Actor", "ForceActorValue", ["Enchanting", 30.0 + 10 * index], "0x14")
            rings.call("console", {"command": "playerenchantobject 3b97c 493aa"})
            wait_for(
                lambda index=index: (
                    p("ObjectReference", "GetItemCount", [{"form": "0x3B97C"}], "0x14")
                    == index + 1
                ),
                message="Creating a restart custom ring failed",
            )
            menu.equip(0x3B97C, 6, silver_name)
            menu.close()
            enchantment = p("WornObject", "GetEnchantment", [{"form": "0x14"}, 0, 64])[
                "formId"
            ]
            enchantments.append(int(enchantment, 16))
            if index == 0:
                assert p(
                    "WornObject",
                    "SetDisplayName",
                    [{"form": "0x14"}, 0, 64, names[0], True],
                )
            menu.equip(0x3B97C, index, names[index])
            menu.close()
        rings.add(0xFCEFD)
        menu.equip(0xFCEFD, 2)
        menu.close()
        assignments = rings.state()["assignments"]
        assert len(assignments) == 3
        assert p("Actor", "GetActorValueMax", ["Health"], "0x14") == player_health + 90
        rings.save(custom_save)
        host_info = rings.client.health()
        saved = {
            "processId": host_info["pid"],
            "executable": host_info["exe"],
            "baseline": rings.baseline,
            "mods": rings.call("inspect", {"kind": "mods"}),
            "actors": npcs,
            "forms": sources,
            "health": health,
            "customAssignments": assignments,
            "customEnchantments": enchantments,
            "customNames": names,
            "playerBaseHealth": player_health,
        }
        path.write_text(json.dumps(saved, indent=2), encoding="utf-8")
        rings.checkpoint(
            "Prepared unused forms and custom rings for a full process restart"
        )
    else:
        saved = json.loads(path.read_text(encoding="utf-8"))
        assert rings.call("inspect", {"kind": "mods"}) == saved["mods"], (
            "Use the same mods and load order for both restart phases"
        )
        rings.restore(empty_save)
        assert not rings.state()["assignments"]
        for npc in saved["actors"]:
            rings.add(0xFCEFD, 10, npc)
        sources = rings.shared_npc_sources(saved["actors"], 0xFCEFD)
        assert sources == saved["forms"], (
            "Restarting allocated new forms instead of reusing the unused pool"
        )
        for npc in saved["actors"]:
            wait_for(
                lambda npc=npc: (
                    p("Actor", "GetActorValueMax", ["Health"], npc)
                    == saved["health"][npc] + 200
                ),
                message="Restarting lost or duplicated NPC ring bonuses",
            )
        name = p("Form", "GetName", self_form="0xFCEFD")
        for source in sources:
            reference = f"0x{source:08X}"
            assert p("Form", "GetName", self_form=reference) == name
            assert p("Armor", "GetEnchantment", self_form=reference) is not None
        rings.restore(custom_save)
        assignments = rings.state()["assignments"]
        assert assignments == saved["customAssignments"]
        assert (
            p("Actor", "GetActorValueMax", ["Health"], "0x14")
            == saved["playerBaseHealth"] + 90
        )
        for index in range(2):
            source = next(
                a["effectSource"] for a in assignments if a["target"] == index
            )
            assert (
                p("Form", "GetName", self_form=f"0x{source:08X}")
                == saved["customNames"][index]
            )
            menu.equip(0x3B97C, 6, saved["customNames"][index])
            menu.close()
            enchantment = p("WornObject", "GetEnchantment", [{"form": "0x14"}, 0, 64])[
                "formId"
            ]
            assert int(enchantment, 16) == saved["customEnchantments"][index]
            assert (
                p("WornObject", "GetDisplayName", [{"form": "0x14"}, 0, 64])
                == saved["customNames"][index]
            )
            menu.equip(0x3B97C, index, saved["customNames"][index])
            menu.close()
            assert (
                p("Actor", "GetActorValueMax", ["Health"], "0x14")
                == saved["playerBaseHealth"] + 90
            )
        p("Actor", "UnequipAll", self_form="0x14")
        wait_for(
            lambda: (
                p("Actor", "GetActorValueMax", ["Health"], "0x14")
                == saved["playerBaseHealth"]
            ),
            message="Unequipping restarted custom rings retained bonuses",
        )
        rings.checkpoint(
            "Unused forms, NPC bonuses and named and unnamed custom rings survive a full Skyrim restart"
        )
