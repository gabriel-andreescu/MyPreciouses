import pytest
from bmk.testing import wait_for

from ...support.session import actor


def test_equipment_effects_and_active_effects_menu(rings):
    p, menu = rings.p, rings.menu
    p("Actor", "UnequipAll", self_form="0x14")
    health_enchantment = p("Armor", "GetEnchantment", self_form="0xFCEFD")["formId"]
    magnitude = p("Enchantment", "GetNthEffectMagnitude", [0], health_enchantment)
    health_effect = int(
        p("Enchantment", "GetNthEffectMagicEffect", [0], health_enchantment)["formId"],
        16,
    )
    krosis_enchantment = p("Armor", "GetEnchantment", self_form="0x61CB9")["formId"]
    krosis_effects = [
        int(
            p("Enchantment", "GetNthEffectMagicEffect", [i], krosis_enchantment)[
                "formId"
            ],
            16,
        )
        for i in range(3)
    ]
    health_spell, krosis_spell = (
        int(health_enchantment, 16),
        int(krosis_enchantment, 16),
    )
    base_health = p("Actor", "GetActorValueMax", ["Health"], "0x14")
    base_values = {
        v: p("Actor", "GetActorValue", [v], "0x14")
        for v in ("MarksmanMod", "LockpickingMod", "AlchemyMod")
    }
    rings.add(0xFCEFD, 3)
    rings.add(0x61CB9)

    def visible_effects():
        key = p("Input", "GetMappedKey", ["Quick Magic", 0])
        assert key >= 0, "Quick Magic must have a keyboard binding"
        rings.keyboard.tap(key)
        try:
            wait_for(
                lambda: p("UI", "GetBool", ["MagicMenu", "_root.Menu_mc.bFadedIn"]),
                message="The magic menu did not become ready",
                timeout=5,
            )
            root = "_root.Menu_mc.inventoryLists.itemList.entryList"
            count = p("UI", "GetInt", ["MagicMenu", f"{root}.length"])
            return [
                p("UI", "GetInt", ["MagicMenu", f"{root}.{i}.formId"]) & 0xFFFFFFFF
                for i in range(count)
                if p("UI", "GetInt", ["MagicMenu", f"{root}.{i}.filterFlag"]) & 256
            ]
        finally:
            if p("UI", "IsMenuOpen", ["MagicMenu"]):
                rings.keyboard.tap(15)
                wait_for(
                    lambda: not p("UI", "IsMenuOpen", ["MagicMenu"]),
                    message="Magic menu did not close",
                )

    def check(count, bonus, mask):
        state = rings.wait(
            lambda s: (
                sum(e["spell"] == health_spell for e in actor(s)["effects"]) == count
                and sum(e["spell"] == krosis_spell for e in actor(s)["effects"])
                == (3 if mask else 0)
            ),
            "Equipment effects were duplicated or retained",
        )
        assert p("Actor", "GetActorValueMax", ["Health"], "0x14") == pytest.approx(
            base_health + bonus, abs=0.01
        )
        effects = [e for e in actor(state)["effects"] if e["spell"] == health_spell]
        assert not any(e["inactive"] or e["dispelled"] for e in effects)
        assert len({e["source"] for e in effects}) == count
        for value, base in base_values.items():
            assert p("Actor", "GetActorValue", [value], "0x14") == pytest.approx(
                base + (20 if mask else 0), abs=0.01
            )
        visible = visible_effects()
        assert visible.count(health_effect) == count, (
            "Active Effects has missing or stale ring entries"
        )
        for effect in krosis_effects:
            assert visible.count(effect) == (1 if mask else 0), (
                "Active Effects has missing or stale Krosis entries"
            )

    p("Actor", "EquipItem", [{"form": "0x61CB9"}, False, True], "0x14")
    for target in (0, 1, 6):
        menu.equip(0xFCEFD, target)
    menu.close()
    check(3, magnitude * 3, True)
    rings.checkpoint("Independent effects for three copies of one enchanted ring")
    rings.settings(iEnchantmentStrengthMode=1, iFixedEnchantmentStrengthPercent=50)
    check(3, magnitude * 1.5, True)
    rings.settings(iEnchantmentStrengthMode=2)
    check(3, magnitude, True)
    rings.settings(iEnchantmentStrengthMode=0)
    check(3, magnitude * 3, True)
    rings.checkpoint(
        "Full, fixed and split ring strength preserve unrelated armor effects"
    )
    rings.save("MyPreciousesTest_Effects")
    rings.settings(iExtraRingMode=1)
    check(1, magnitude, True)
    rings.restore("MyPreciousesTest_Effects")
    check(1, magnitude, True)
    rings.checkpoint(
        "Loading a functional-mode save in cosmetic mode removes virtual bonuses"
    )
    rings.settings(iExtraRingMode=0)
    check(3, magnitude * 3, True)
    rings.checkpoint(
        "Cosmetic mode removes and functional mode restores virtual enchantments"
    )
    rings.save("MyPreciousesTest_Effects")
    for _ in range(3):
        rings.restore("MyPreciousesTest_Effects")
        check(3, magnitude * 3, True)
        p("Actor", "UnequipItem", [{"form": "0x61CB9"}, False, True], "0x14")
        p("Actor", "UnequipAll", self_form="0x14")
        check(0, 0, False)
    rings.checkpoint(
        "Repeated loads and unequips leave no ring or Krosis effects or menu entries"
    )
