from bmk.testing import wait_for

from ...support.session import assigned


def test_granted_spells(rings):
    p, menu = rings.p, rings.menu
    p("Actor", "UnequipAll", self_form="0x14")
    form = rings.client.form_id
    hircine = 0x2AC60
    power = {"form": "0xF8306"}
    arcana = form(0x1DB9B, "Dragonborn.esm")
    ignite = {"form": f"0x{form(0x2732B, 'Dragonborn.esm'):08X}"}
    freeze = {"form": f"0x{form(0x2732D, 'Dragonborn.esm'):08X}"}
    rings.add(hircine)
    menu.equip(hircine, 0)
    menu.close()
    assert not p("Actor", "HasSpell", [power], "0x14"), (
        "Hircine granted Beast Form to a non-werewolf"
    )
    p("Actor", "UnequipAll", self_form="0x14")
    rings.call("console", {"command": "setpqv C00 PlayerHasBeastBlood 1"})
    menu.equip(hircine, 0)
    menu.close()
    wait_for(
        lambda: p("Actor", "HasSpell", [power], "0x14"),
        message="Hircine did not grant its power to an eligible player",
    )
    rings.add(arcana)
    menu.equip(arcana, 1)
    menu.close()
    wait_for(
        lambda: (
            p("Actor", "HasSpell", [ignite], "0x14")
            and p("Actor", "HasSpell", [freeze], "0x14")
        ),
        message="Arcana did not grant Ignite and Freeze",
    )
    p("Actor", "EquipSpell", [ignite, 0], "0x14")
    p("Actor", "EquipSpell", [freeze, 1], "0x14")
    rings.save("MyPreciousesTest_GrantedSpells")
    rings.restore("MyPreciousesTest_GrantedSpells")
    assert p("Actor", "GetEquippedSpell", [0], "0x14")["formId"] == ignite["form"]
    assert p("Actor", "GetEquippedSpell", [1], "0x14")["formId"] == freeze["form"]

    def spells(expected):
        return all(
            p("Actor", "HasSpell", [spell], "0x14") == expected
            for spell in (power, ignite, freeze)
        )

    rings.settings(iExtraRingMode=1)
    wait_for(
        lambda: spells(False), message="Cosmetic mode retained ring-granted spells"
    )
    assert p("Actor", "GetEquippedSpell", [0], "0x14") is None
    assert p("Actor", "GetEquippedSpell", [1], "0x14") is None
    rings.settings(iExtraRingMode=0)
    wait_for(
        lambda: spells(True),
        message="Functional mode did not restore ring-granted spells",
    )
    p("Actor", "UnequipAll", self_form="0x14")
    wait_for(lambda: spells(False), message="Unequipping retained ring-granted spells")
    rings.checkpoint(
        "Hircine eligibility and Arcana spell grants, equipped hands, cosmetic mode and cleanup survive save/load"
    )


def test_hircine_transformation_and_frostmoon_hunt(rings):
    p, menu = rings.p, rings.menu
    p("Actor", "UnequipAll", self_form="0x14")
    race = int(p("Actor", "GetRace", self_form="0x14")["formId"], 16)
    rings.call("console", {"command": f"setpqv C00 PlayerOriginalRace 0x{race:08X}"})
    rings.call("console", {"command": "setpqv C00 PlayerHasBeastBlood 1"})
    hunt = rings.client.form_id(0x275B9, "Dragonborn.esm")
    bonus = {"form": f"0x{rings.client.form_id(0x35B1E, 'Dragonborn.esm'):08X}"}
    power = {"form": "0xF8306"}
    rings.add(0x2AC60)
    rings.add(hunt)
    menu.equip(0x2AC60, 0)
    menu.equip(hunt, 1)
    menu.close()
    wait_for(
        lambda: p("Actor", "HasSpell", [power], "0x14"),
        message="Hircine power is unavailable",
    )
    rings.transform(0xF8306, 0xCDD84)
    wait_for(
        lambda: p("Actor", "HasSpell", [bonus], "0x14"),
        message="Hircine transformation lost the Frostmoon Hunt bonus",
    )
    rings.revert("PlayerWerewolfQuest", race)
    rings.wait(
        lambda s: assigned(s, 0x2AC60, 0) and assigned(s, hunt, 1),
        "Hircine transformation lost ring assignments",
    )
    wait_for(
        lambda: (
            p("Actor", "HasSpell", [power], "0x14")
            and not p("Actor", "HasSpell", [bonus], "0x14")
        ),
        message="Returning from Hircine transformation left incorrect powers",
    )
    rings.checkpoint(
        "Hircine transformation activates Frostmoon effects and restores the rings and reusable power on return"
    )
