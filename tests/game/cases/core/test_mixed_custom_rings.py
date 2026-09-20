import time

from bmk.testing import wait_for

from ...support.session import actor


def test_mixed_ring_stack(rings):
    p, menu = rings.p, rings.menu
    p("Actor", "UnequipAll", self_form="0x14")
    silver = 0x3B97C
    silver_name = p("Form", "GetName", self_form="0x3B97C")
    named = "MyPreciouses Test Named Silver"
    base_health = p("Actor", "GetActorValueMax", ["Health"], "0x14")
    enchanted = []
    for magnitude in (30, 40):
        p("Actor", "ForceActorValue", ["Enchanting", float(magnitude)], "0x14")
        rings.call("console", {"command": "playerenchantobject 3b97c 493aa"})
        time.sleep(0.3)
        menu.equip(silver, 6, silver_name)
        menu.close()
        enchantment = p("WornObject", "GetEnchantment", [{"form": "0x14"}, 0, 64])[
            "formId"
        ]
        assert p("Enchantment", "GetNthEffectMagnitude", [0], enchantment) == magnitude
        enchanted.append(int(enchantment, 16))
        if magnitude == 30:
            assert p(
                "WornObject", "SetDisplayName", [{"form": "0x14"}, 0, 64, named, True]
            )
        p("Actor", "UnequipAll", self_form="0x14")
    rings.add(silver, 2)
    rings.add(0xFCEFD)
    vanilla_enchantment = p("Armor", "GetEnchantment", self_form="0xFCEFD")["formId"]
    vanilla_magnitude = p(
        "Enchantment", "GetNthEffectMagnitude", [0], vanilla_enchantment
    )
    expected_health = base_health + 70 + vanilla_magnitude
    menu.equip(silver, 0, enchantment_id=0)
    menu.equip(silver, 1, enchantment_id=enchanted[1])
    menu.equip(silver, 2, named)
    menu.equip(0xFCEFD, 3)
    menu.equip(silver, 6, enchantment_id=0)
    menu.close()
    mixed = rings.state()
    assert len(mixed["assignments"]) == 4 and actor(mixed)["rightWorn"] == silver
    assert (
        sum(
            a["kind"] == 2 and a["enchantment"] in enchanted
            for a in mixed["assignments"]
        )
        == 2
    )
    assert p("Actor", "GetActorValueMax", ["Health"], "0x14") == expected_health
    for index in range(2):
        menu.equip(silver, 6, enchantment_id=enchanted[index])
        menu.close()
        assert (
            p("WornObject", "GetEnchantment", [{"form": "0x14"}, 0, 64])["formId"]
            == f"0x{enchanted[index]:08X}"
        )
        assert p("Actor", "GetActorValueMax", ["Health"], "0x14") == expected_health
        menu.equip(silver, 4 + index, enchantment_id=enchanted[index])
        menu.equip(silver, 6, enchantment_id=0)
        menu.close()
    before = rings.state()["assignments"]
    rings.save("MyPreciousesTest_MixedCustomRings")
    rings.restore("MyPreciousesTest_MixedCustomRings")
    assert rings.state()["assignments"] == before
    assert p("Actor", "GetActorValueMax", ["Health"], "0x14") == expected_health
    menu.select(silver, named, enchanted[0])
    menu.select(silver, silver_name, enchanted[1])
    menu.close()
    rings.checkpoint(
        "Plain, unnamed enchanted, named enchanted and vanilla enchanted rings coexist through native moves and save/load"
    )
    total_bonus = expected_health - base_health
    for cycle in range(3):
        for strength, bonus in (
            (1, total_bonus * 0.5),
            (2, total_bonus / 3),
            (0, total_bonus),
        ):
            rings.settings(
                iEnchantmentStrengthMode=strength, iFixedEnchantmentStrengthPercent=50
            )
            wait_for(
                lambda bonus=bonus: (
                    abs(
                        p("Actor", "GetActorValueMax", ["Health"], "0x14")
                        - base_health
                        - bonus
                    )
                    < 0.01
                ),
                message="Changing strength lost or duplicated a mixed-ring bonus",
            )
            effects = [
                e
                for e in actor(rings.state())["effects"]
                if e["spell"] in [*enchanted, int(vanilla_enchantment, 16)]
            ]
            assert len(effects) == 3 and not any(
                e["inactive"] or e["dispelled"] for e in effects
            )
        rings.settings(iExtraRingMode=1)
        wait_for(
            lambda: p("Actor", "GetActorValueMax", ["Health"], "0x14") == base_health,
            message="Cosmetic mixed rings retained their bonuses",
        )
        if cycle == 0:
            rings.save("MyPreciousesTest_MixedCustomCosmetic")
            rings.restore("MyPreciousesTest_MixedCustomCosmetic")
            assert p("Actor", "GetActorValueMax", ["Health"], "0x14") == base_health
        rings.settings(iExtraRingMode=0)
        wait_for(
            lambda: (
                p("Actor", "GetActorValueMax", ["Health"], "0x14") == expected_health
            ),
            message="Restoring functional mixed rings lost their bonuses",
        )
        menu.select(silver, named, enchanted[0])
        menu.select(silver, silver_name, enchanted[1])
        menu.close()
    rings.checkpoint(
        "Mixed custom rings preserve names and separate effects through strength changes and cosmetic save/load"
    )
    menu.equip(silver, 4, enchantment_id=0)
    menu.close()
    assert p("Actor", "GetActorValueMax", ["Health"], "0x14") == expected_health - 30
    p("Actor", "UnequipAll", self_form="0x14")
    rings.wait(
        lambda s: not s["assignments"] and actor(s)["rightWorn"] == 0,
        "Clearing mixed rings retained equipment",
    )
    assert p("Actor", "GetActorValueMax", ["Health"], "0x14") == base_health
    rings.checkpoint(
        "Replacing and clearing mixed ring copies removes only their own bonuses"
    )
