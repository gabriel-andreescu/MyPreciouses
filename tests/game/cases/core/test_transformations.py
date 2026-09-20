import time

import pytest
from bmk.testing import wait_for

from ...support.session import actor


def test_frostmoon_werewolf_effects(rings):
    p, menu = rings.p, rings.menu
    p("Actor", "UnequipAll", self_form="0x14")
    original_race = p("Actor", "GetRace", self_form="0x14")["formId"]
    assert original_race != "0x000CDD84", "The baseline must be in human form"
    form = rings.client.form_id
    frostmoon = [
        form(i, "Dragonborn.esm") for i in (0x275B6, 0x275B8, 0x275B7, 0x275B9)
    ]
    bloodlust = {"form": f"0x{form(0x35B17, 'Dragonborn.esm'):08X}"}
    moon = {"form": f"0x{form(0x35B16, 'Dragonborn.esm'):08X}"}
    hunt = {"form": f"0x{form(0x35B1E, 'Dragonborn.esm'):08X}"}
    instinct = form(0x35B1B, "Dragonborn.esm")
    for target, ring in enumerate(frostmoon):
        rings.add(ring)
        menu.equip(ring, target)
    menu.close()
    before = rings.state()["assignments"]
    # C00 normally records this when joining the Companions. The test save skips that quest.
    rings.call("console", {"command": f"setpqv C00 PlayerOriginalRace {original_race}"})
    rings.transform(0x92C48, 0xCDD84)

    def bonuses(expected):
        return (
            p("Actor", "HasPerk", [bloodlust], "0x14") == expected
            and p("Actor", "HasPerk", [moon], "0x14") == expected
            and p("Actor", "HasSpell", [hunt], "0x14") == expected
        )

    wait_for(
        lambda: bonuses(True),
        message="The Frostmoon perks and regeneration spell are missing in Beast Form",
    )
    rings.wait(
        lambda s: any(
            e["spell"] == instinct and not e["inactive"] and not e["dispelled"]
            for e in actor(s)["effects"]
        ),
        "The Instinct ring did not slow time on transformation",
    )
    rings.save("MyPreciousesTest_Werewolf")
    rings.restore("MyPreciousesTest_Werewolf")
    assert actor(rings.state())["race"] == 0xCDD84
    assert bonuses(True), "Loading in Beast Form lost Frostmoon effects"
    rings.checkpoint(
        "Frostmoon rings retain their werewolf effects through transformation and save/load"
    )
    rings.settings(iExtraRingMode=1)
    wait_for(
        lambda: bonuses(False),
        message="Cosmetic mode retained suspended Frostmoon bonuses",
    )
    rings.settings(iExtraRingMode=0)
    wait_for(
        lambda: bonuses(True),
        message="Functional mode did not restore suspended Frostmoon bonuses",
    )
    rings.revert("PlayerWerewolfQuest", int(original_race, 16))
    rings.wait(
        lambda s: (
            len(s["assignments"]) == 4
            and sum(v["thirdPersonGeometry"] > 0 for v in actor(s)["visuals"]) == 4
        ),
        "Returning from Beast Form lost ring assignments or visuals",
    )
    assert rings.state()["assignments"] == before
    wait_for(
        lambda: bonuses(False),
        message="Returning to human form retained werewolf-only bonuses",
    )
    rings.checkpoint(
        "Cosmetic mode and returning to human form clean up Frostmoon effects and restore rings"
    )


def test_vampire_lord_native_virtual_and_cosmetic_bonuses(rings):
    p, menu = rings.p, rings.menu
    form = rings.client.form_id
    power = form(0x283B, "Dawnguard.esm")
    race = form(0x283A, "Dawnguard.esm")
    quest = f"0x{(race & 0xFF000000) + 0x71D0:08X}"
    beast = form(0xE7FD, "Dawnguard.esm")
    erudite = form(0xE7FE, "Dawnguard.esm")
    beast_flag = f"0x{form(0x14627, 'Dawnguard.esm'):08X}"
    erudite_flag = f"0x{form(0x14628, 'Dawnguard.esm'):08X}"
    values = {}
    for placement in ("Empty", "NativeBeast", "NativeErudite", "Virtual", "Cosmetic"):
        rings.restore()
        p("Actor", "UnequipAll", self_form="0x14")
        rings.settings(
            iExtraRingMode=1 if placement == "Cosmetic" else 0,
            iEnchantmentStrengthMode=0,
        )
        original = int(p("Actor", "GetRace", self_form="0x14")["formId"], 16)
        if placement != "Empty":
            rings.add(beast)
            rings.add(erudite)
            if placement == "NativeBeast":
                menu.equip(beast, 6)
            elif placement == "NativeErudite":
                menu.equip(erudite, 6)
            else:
                menu.equip(beast, 0)
                menu.equip(erudite, 1)
                menu.close()
                for ring in (beast, erudite):
                    assert p(
                        "Actor", "IsEquipped", [{"form": f"0x{ring:08X}"}], "0x14"
                    ) == (placement == "Virtual")
        menu.close()
        before = rings.state()["assignments"]
        rings.transform(power, race)
        # The race event precedes Dawnguard's equipment removal and leveled abilities.
        wait_for(
            lambda: p("Quest", "GetStage", self_form=quest) == 10,
            message="Vampire Lord initialization did not finish",
            timeout=20,
        )
        # Dawnguard's three-second update releases the transformation's save lock.
        time.sleep(4)
        assert p("GlobalVariable", "GetValue", self_form=beast_flag) == int(
            placement in ("NativeBeast", "Virtual")
        )
        assert p("GlobalVariable", "GetValue", self_form=erudite_flag) == int(
            placement in ("NativeErudite", "Virtual")
        )
        current = {
            av: p("Actor", "GetActorValue", [av], "0x14")
            for av in ("Health", "Magicka", "UnarmedDamage", "MagickaRate")
        }
        values[placement] = current
        if placement == "Virtual":
            rings.save("MyPreciousesTest_VampireLord")
            rings.restore("MyPreciousesTest_VampireLord")
            assert actor(rings.state())["race"] == race
            for av, value in current.items():
                assert p("Actor", "GetActorValue", [av], "0x14") == pytest.approx(
                    value, abs=0.01
                ), av
        rings.revert("DLC1PlayerVampireQuest", original)
        wait_for(
            lambda: (
                p("GlobalVariable", "GetValue", self_form=beast_flag) == 0
                and p("GlobalVariable", "GetValue", self_form=erudite_flag) == 0
            ),
            message="Returning from Vampire Lord retained the ring flags",
        )
        if placement in ("Virtual", "Cosmetic"):
            rings.wait(
                lambda s: len(s["assignments"]) == 2,
                "Vampire Lord return lost virtual rings",
            )
            assert rings.state()["assignments"] == before
    for av in ("Health", "UnarmedDamage"):
        assert values["Virtual"][av] == pytest.approx(
            values["NativeBeast"][av], abs=0.01
        ), av
    for av in ("Magicka", "MagickaRate"):
        assert values["Virtual"][av] == pytest.approx(
            values["NativeErudite"][av], abs=0.01
        ), av
    for av, value in values["Empty"].items():
        assert values["Cosmetic"][av] == pytest.approx(value, abs=0.01), av
    rings.checkpoint(
        "Beast and Erudite match native Vampire Lord bonuses, respect cosmetic mode, and survive save/load and return"
    )
