from bmk.testing import wait_for

from ...support.session import assigned


def exercise_custom_identity(rings):
    p, menu = rings.p, rings.menu
    rings.restore()
    p("Actor", "UnequipAll", self_form="0x14")
    rings.settings(iExtraRingMode=0, iEnchantmentStrengthMode=0, bAlwaysChooseFinger=1)
    silver = 0x3B97C
    silver_name = p("Form", "GetName", self_form="0x3B97C")
    health = p("Actor", "GetActorValueMax", ["Health"], "0x14")
    names = [
        "MyPreciouses Identical Enchantment One",
        "MyPreciouses Identical Enchantment Two",
    ]
    enchantments = []
    p("Actor", "ForceActorValue", ["Enchanting", 30.0], "0x14")
    for name in names:
        rings.call("console", {"command": "playerenchantobject 3b97c 493aa"})
        expected = len(enchantments) + 1
        rings.wait(
            lambda s, expected=expected: (
                sum(
                    i["formId"] == silver and i["count"] == expected
                    for i in s["inventory"]
                )
                == 1
            ),
            "Creating an identical custom ring failed",
        )
        menu.equip(silver, 6, silver_name)
        menu.close()
        enchantment = p("WornObject", "GetEnchantment", [{"form": "0x14"}, 0, 64])[
            "formId"
        ]
        assert p("Enchantment", "GetNthEffectMagnitude", [0], enchantment) == 30
        enchantments.append(int(enchantment, 16))
        assert p("WornObject", "SetDisplayName", [{"form": "0x14"}, 0, 64, name, True])
        p("Actor", "UnequipAll", self_form="0x14")
    assert enchantments[0] == enchantments[1], (
        "This case requires Skyrim to reuse the identical enchantment"
    )
    menu.equip(silver, 0, names[0])
    menu.equip(silver, 1, names[1])
    menu.close()
    for name, text in ((names[0], f"{names[0]} (L Thumb)"), (names[1], names[1])):
        menu.select(silver, name)
        assert menu.ui("GetString", f"{menu.list}.selectedEntry.text") == text
        assert menu.ui("GetInt", f"{menu.list}.selectedEntry.equipState") == 2
        menu.close()
    before = rings.state()["assignments"]
    assert len(before) == 2 and len({a["uniqueId"] for a in before}) == 2
    assert p("Actor", "GetActorValueMax", ["Health"], "0x14") == health + 60
    rings.save("MyPreciousesTest_CustomNames")
    rings.restore("MyPreciousesTest_CustomNames")
    assert rings.state()["assignments"] == before
    for target in (6, 4):
        menu.equip(silver, target, names[0])
        menu.close()
        if target == 6:
            assert (
                p("WornObject", "GetDisplayName", [{"form": "0x14"}, 0, 64]) == names[0]
            )
        menu.open_selector(silver, target, names[1])
        assert (
            menu.ui(
                "GetString", f"{menu.selector}.fingerRows.{target % 5}.baseEquippedRing"
            )
            == names[0]
        )
        menu.close()
        assert assigned(rings.state(), silver, 1)
        assert p("Actor", "GetActorValueMax", ["Health"], "0x14") == health + 60
    menu.equip(silver, 1, names[0])
    menu.close()
    assert len(rings.state()["assignments"]) == 1
    assert p("Actor", "GetActorValueMax", ["Health"], "0x14") == health + 30
    menu.select(silver, names[1])
    menu.close()
    p("Actor", "UnequipAll", self_form="0x14")
    wait_for(
        lambda: p("Actor", "GetActorValueMax", ["Health"], "0x14") == health,
        message="Clearing identical custom rings retained a bonus",
    )
    rings.checkpoint(
        "Identical custom enchantments with different names retain separate copies through moves, replacement and save/load"
    )


def test_identically_enchanted_named_rings(rings):
    exercise_custom_identity(rings)
