import time

from bmk.testing import wait_for


def test_inventory_confiscation(rings):
    p, menu = rings.p, rings.menu
    p("Actor", "UnequipAll", self_form="0x14")
    silver, namira, health_ring = 0x3B97C, 0x2C37B, 0xFCEFD
    perk = {"form": "0xEE5C3"}
    health = p("Actor", "GetActorValueMax", ["Health"], "0x14")
    p("Actor", "ForceActorValue", ["Enchanting", 35.0], "0x14")
    rings.call("console", {"command": "playerenchantobject 3b97c 493aa"})
    time.sleep(0.3)
    menu.equip(silver, 6)
    menu.close()
    enchantment = p("WornObject", "GetEnchantment", [{"form": "0x14"}, 0, 64])["formId"]
    assert p("Enchantment", "GetNthEffectMagnitude", [0], enchantment) == 35
    name = "MyPreciouses Test Confiscated Silver"
    assert p("WornObject", "SetDisplayName", [{"form": "0x14"}, 0, 64, name, True])
    p("Actor", "UnequipAll", self_form="0x14")
    for form in (silver, namira, health_ring):
        rings.add(form)
    container = p(
        "ObjectReference", "PlaceAtMe", [{"form": "0xC2CD4"}, 1, True, False], "0x14"
    )["formId"]
    assert container, "The test container was not created"
    p("ObjectReference", "RemoveAllItems", self_form=container)

    def equip():
        menu.equip(silver, 0, name)
        menu.equip(silver, 1, enchantment_id=0)
        menu.equip(namira, 2)
        menu.equip(health_ring, 3)
        menu.close()
        wait_for(
            lambda: p("Actor", "HasPerk", [perk], "0x14"),
            message="Namira did not grant its perk",
        )

    for unequip_first in (False, True):
        equip()
        equipped_health = p("Actor", "GetActorValueMax", ["Health"], "0x14")
        assert equipped_health > health
        counts = {
            form: p(
                "ObjectReference", "GetItemCount", [{"form": f"0x{form:08X}"}], "0x14"
            )
            for form in (silver, namira, health_ring)
        }
        if unequip_first:
            p("Actor", "UnequipAll", self_form="0x14")
        p(
            "ObjectReference",
            "RemoveAllItems",
            [{"form": container}, True, False],
            "0x14",
        )
        rings.wait(
            lambda s: not s["assignments"] and not s["scriptBindings"],
            "Confiscation retained virtual equipment",
        )
        wait_for(
            lambda: not p("Actor", "HasPerk", [perk], "0x14"),
            message="Confiscation retained Namira perk",
        )
        wait_for(
            lambda: p("Actor", "GetActorValueMax", ["Health"], "0x14") == health,
            message="Confiscation retained enchantments",
        )
        for form, count in counts.items():
            reference = {"form": f"0x{form:08X}"}
            assert p("ObjectReference", "GetItemCount", [reference], "0x14") == 0
            assert p("ObjectReference", "GetItemCount", [reference], container) == count
        rings.save("MyPreciousesTest_Confiscated")
        rings.restore("MyPreciousesTest_Confiscated")
        p(
            "ObjectReference",
            "RemoveAllItems",
            [{"form": "0x14"}, True, False],
            container,
        )
        for form, count in counts.items():
            assert (
                p(
                    "ObjectReference",
                    "GetItemCount",
                    [{"form": f"0x{form:08X}"}],
                    "0x14",
                )
                == count
            )
        assert not rings.state()["assignments"], (
            "Returning unequipped inventory unexpectedly equipped virtual rings"
        )
        equip()
        wait_for(
            lambda equipped_health=equipped_health: (
                p("Actor", "GetActorValueMax", ["Health"], "0x14") == equipped_health
            ),
            message="Returned rings lost or duplicated bonuses",
        )
        assert (
            sum(
                a["enchantment"] == int(enchantment, 16)
                for a in rings.state()["assignments"]
            )
            == 1
        )
        p("Actor", "UnequipAll", self_form="0x14")
        wait_for(
            lambda: (
                p("Actor", "GetActorValueMax", ["Health"], "0x14") == health
                and not p("Actor", "HasPerk", [perk], "0x14")
            ),
            message="Returned rings did not clean up on unequip",
        )
        rings.checkpoint(
            f"Full inventory confiscation, save/load, return and re-equip with UnequipAll={unequip_first}"
        )
