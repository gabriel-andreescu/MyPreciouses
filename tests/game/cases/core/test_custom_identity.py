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
    exercise_identical_rows(rings)


def exercise_identical_rows(rings):
    rings.restore()
    p, menu = rings.p, rings.menu
    silver = 0x3B97C
    rings.settings(iExtraRingMode=0, iEnchantmentStrengthMode=0, bAlwaysChooseFinger=1)
    p("Actor", "UnequipAll", self_form="0x14")
    health = p("Actor", "GetActorValueMax", ["Health"], "0x14")
    p("Actor", "ForceActorValue", ["Enchanting", 30.0], "0x14")
    for count in (1, 2):
        rings.call("console", {"command": "playerenchantobject 3b97c 493aa"})
        rings.wait(
            lambda state, count=count: any(
                item["formId"] == silver and item["count"] == count
                for item in state["inventory"]
            ),
            "Identical rings were not added",
        )
    menu.equip(silver, 6)
    menu.close()
    p("WornObject", "SetItemHealthPercent", [{"form": "0x14"}, 0, 64, 1.1])
    assert (
        abs(p("WornObject", "GetItemHealthPercent", [{"form": "0x14"}, 0, 64]) - 1.1)
        < 0.001
    )

    def rows():
        menu.open()
        result = []
        for index in range(menu.ui("GetInt", f"{menu.list}.entryList.length")):
            row = f"{menu.list}.entryList.{index}"
            if menu.ui("GetInt", f"{row}.formId") == silver:
                result.append(
                    {
                        "id": menu.ui("GetInt", f"{row}.myPreciousesCustomUniqueID"),
                        "native": menu.ui(
                            "GetBool", f"{row}.myPreciousesVanillaRingSlotEquipped"
                        ),
                        "count": menu.ui("GetInt", f"{row}.count"),
                        "equipped": menu.ui("GetInt", f"{row}.equipState"),
                    }
                )
        return result

    separate = rows()
    assert len(separate) == 2 and all(row["count"] == 1 for row in separate)
    native = next(row for row in separate if row["native"])
    other = next(row for row in separate if not row["native"])
    menu.equip(silver, 3, unique_id=other["id"])
    menu.equip(silver, 8, unique_id=native["id"])
    menu.close()

    def check_copies():
        state = rings.state()
        assignments = {
            item["uniqueId"]: item["target"] for item in state["assignments"]
        }
        assert len(assignments) == 2 and set(assignments.values()) == {3, 8}
        current = rows()
        assert len(current) == 2 and all(
            row["count"] == 1 and not row["native"] for row in current
        )
        for row in current:
            own_target = assignments[row["id"]]
            assert row["equipped"] == (2 if own_target == 3 else 3)
            for target in (3, 8):
                menu.open_selector(silver, target, unique_id=row["id"])
                assert menu.ui(
                    "GetString", f"{menu.selector}.fingerRows.{target % 5}.actionLabel"
                ) == ("Unequip" if target == own_target else "Replace")
                menu.ui("Invoke", f"{menu.selector}.CancelSelection")
            menu.close()
        assert p("Actor", "GetActorValueMax", ["Health"], "0x14") == health + 60
        return assignments

    menu.equip(silver, 6, unique_id=native["id"])
    menu.close()
    p("WornObject", "SetItemHealthPercent", [{"form": "0x14"}, 0, 64, 1.0])
    assert p("WornObject", "GetItemHealthPercent", [{"form": "0x14"}, 0, 64]) == 1.0
    menu.equip(silver, 8, unique_id=native["id"])
    menu.close()
    before = check_copies()
    rings.save("MyPreciousesTest_IdenticalRows")
    rings.restore("MyPreciousesTest_IdenticalRows")
    assert check_copies() == before
    left = next(identity for identity, target in before.items() if target == 3)
    right = next(identity for identity, target in before.items() if target == 8)
    menu.equip(silver, 3, unique_id=right)
    menu.close()
    state = rings.state()
    assert (
        len(state["assignments"]) == 1 and state["assignments"][0]["uniqueId"] == right
    )
    current = rows()
    assert next(row for row in current if row["id"] == left)["equipped"] == 0
    menu.equip(silver, 8, unique_id=left)
    menu.close()
    check_copies()
    before = rings.state()["assignments"]
    menu.open_selector(silver, 0, unique_id=left)
    rings.add(0x1CF2B)
    menu.ui("Invoke", f"{menu.selector}.EquipSelection")
    rings.wait(lambda state: not state["selectorOpen"], "Stale selector remained open")
    menu.close()
    assert rings.state()["assignments"] == before, "Stale selection equipped a ring"

    menu.equip(silver, 0, unique_id=left)
    menu.close()
    current = rows()
    assert all(row["equipped"] == 2 and not row["native"] for row in current)
    assert {a["target"] for a in rings.state()["assignments"]} == {0, 3}

    menu.select(silver, unique_id=left)
    menu.ui("Invoke", "_root.Menu_mc.DropItem")
    menu.close()
    rings.wait(
        lambda state: (
            len(state["assignments"]) == 1
            and state["assignments"][0]["uniqueId"] == right
        ),
        "Dropping one equipped copy changed the other copy",
    )
    assert p("Actor", "GetActorValueMax", ["Health"], "0x14") == health + 30

    rings.restore("MyPreciousesTest_IdenticalRows")
    menu.equip(silver, 3, unique_id=right)
    menu.close()
    menu.select(silver, unique_id=left)
    menu.ui("Invoke", "_root.Menu_mc.DropItem")
    menu.close()
    rings.wait(
        lambda state: any(
            item["formId"] == silver and item["count"] == 1
            for item in state["inventory"]
        ),
        "Dropping the unequipped copy did not remove it",
    )
    remaining = rings.state()["assignments"]
    assert len(remaining) == 1 and remaining[0]["uniqueId"] == right
    assert p("Actor", "GetActorValueMax", ["Health"], "0x14") == health + 30

    rings.restore("MyPreciousesTest_IdenticalRows")
    menu.equip(silver, 6, unique_id=right)
    menu.close()
    rings.call("console", {"command": "playerenchantobject 3b97c 493aa"})
    rings.wait(
        lambda state: any(
            item["formId"] == silver and item["count"] == 3
            for item in state["inventory"]
        ),
        "The third identical copy was not added",
    )
    remaining = rings.state()["assignments"]
    assert len(remaining) == 1 and remaining[0]["uniqueId"] == left
    current = rows()
    assert sum(row["count"] for row in current) == 3
    assert sum(row["count"] for row in current if row["native"]) == 1
    assert p("Actor", "GetActorValueMax", ["Health"], "0x14") == health + 60
    menu.close()
    rings.checkpoint(
        "Identical copies retain exact equip state through moves, replacement, stale actions, removal, save/load and adding an unequipped spare"
    )
