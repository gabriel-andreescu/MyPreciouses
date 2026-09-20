def test_named_custom_enchantments(rings):
    p, menu = rings.p, rings.menu
    ring = 0x3B97C
    p("Actor", "UnequipAll", self_form="0x14")
    name = p("Form", "GetName", self_form="0x3B97C")
    health = p("Actor", "GetActorValueMax", ["Health"], "0x14")
    assert not any(i["formId"] == ring for i in rings.state()["inventory"]), (
        "The baseline must not contain silver rings"
    )
    custom = []
    for magnitude in (30, 50):
        p("Actor", "ForceActorValue", ["Enchanting", float(magnitude)], "0x14")
        rings.call("console", {"command": "playerenchantobject 3b97c 493aa"})
        expected = len(custom) + 1
        rings.wait(
            lambda s, expected=expected: (
                sum(
                    i["formId"] == ring and i["count"] == expected
                    for i in s["inventory"]
                )
                == 1
            ),
            "The console did not create a custom enchanted ring",
        )
        menu.equip(ring, 6, name)
        menu.close()
        custom_name = f"MyPreciouses Test Health {magnitude}"
        assert p(
            "WornObject", "SetDisplayName", [{"form": "0x14"}, 0, 64, custom_name, True]
        )
        enchantment = p("WornObject", "GetEnchantment", [{"form": "0x14"}, 0, 64])[
            "formId"
        ]
        assert p("Enchantment", "GetNthEffectMagnitude", [0], enchantment) == magnitude
        custom.append({"name": custom_name, "enchantment": int(enchantment, 16)})
        p("Actor", "UnequipAll", self_form="0x14")
    for target in range(2):
        menu.equip(ring, target, custom[target]["name"])
    menu.close()
    before = rings.state()["assignments"]
    assert len(before) == 2 and all(a["kind"] == 2 for a in before)
    assert len({a["enchantment"] for a in before}) == 2
    assert len({a["uniqueId"] for a in before}) == 2
    assert p("Actor", "GetActorValueMax", ["Health"], "0x14") == health + 80
    for target in (2, 7, 2):
        menu.equip(ring, target, custom[0]["name"])
        menu.close()
        moved = rings.state()["assignments"]
        assert len(moved) == 2 and not any(a["target"] == 0 for a in moved)
        for original in before:
            current = [a for a in moved if a["enchantment"] == original["enchantment"]]
            assert len(current) == 1 and current[0]["uniqueId"] == original["uniqueId"]
        assert p("Actor", "GetActorValueMax", ["Health"], "0x14") == health + 80
    rings.save("MyPreciousesTest_CustomRings")
    rings.restore("MyPreciousesTest_CustomRings")
    assert rings.state()["assignments"] == moved
    assert p("Actor", "GetActorValueMax", ["Health"], "0x14") == health + 80
    rings.checkpoint(
        "Named custom enchantments retain separate copies, moves and save data"
    )
    menu.equip(ring, 1, custom[0]["name"])
    menu.close()
    replaced = rings.state()["assignments"]
    assert len(replaced) == 1 and replaced[0]["enchantment"] == custom[0]["enchantment"]
    assert p("Actor", "GetActorValueMax", ["Health"], "0x14") == health + 30
    p("Actor", "UnequipAll", self_form="0x14")
    rings.wait(lambda s: not s["assignments"], "UnequipAll retained a custom ring")
    assert p("Actor", "GetActorValueMax", ["Health"], "0x14") == health
    rings.checkpoint(
        "Replacing and clearing custom rings removes displaced enchantment effects"
    )
