import pytest
from bmk.testing import wait_for


@pytest.mark.parametrize(
    "orf",
    [False, pytest.param(True, marks=pytest.mark.compatibility("ORF"))],
    ids=["vanilla", "ORF"],
)
def test_engine_equipment_conditions(rings, orf):
    p, menu = rings.p, rings.menu
    p("Actor", "UnequipAll", self_form="0x14")
    plugin, local_id = ("ORF Rings.esp", 0x800) if orf else ("Skyrim.esm", 0x3B97C)
    ring_id = rings.client.form_id(local_id, plugin)
    ring = {"form": f"0x{ring_id:08X}"}
    rings.add(ring_id)
    condition_form = {"form": "0x493AA"}
    assert not p("PO3_SKSEFunctions", "GetConditionList", [condition_form, 0]), (
        "The test requires unmodified EnchFortifyHealthConstantSelf conditions"
    )
    list_size = p("FormList", "GetSize", self_form="0xD30D2")
    list_match = p("FormList", "HasForm", [ring], "0xD30D2")
    clone = p("Form", "TempClone", self_form="0xD30D2")
    assert clone and clone["formId"]
    p("FormList", "AddForm", [ring], clone["formId"])
    assert p("FormList", "HasForm", [ring], clone["formId"])
    assert p("FormList", "GetSize", self_form="0xD30D2") == list_size
    assert p("FormList", "HasForm", [ring], "0xD30D2") == list_match
    cases = [
        ("GetEquipped", f"0x{local_id:X} ~ {plugin}", ring["form"]),
        (
            "WornHasKeyword",
            "0x80C ~ Outfit Recognition Framework.esp"
            if orf
            else "0x6BBE9 ~ Skyrim.esm",
            ring["form"],
        ),
        ("GetEquipped", clone["formId"], clone["formId"]),
    ]
    for function, argument, equipped_form in cases:
        condition = f"Subject | {function} | {argument} | NONE | == | 1.0 | AND"

        def evaluate():
            return p(
                "PO3_SKSEFunctions",
                "EvaluateConditionList",
                [condition_form, {"form": "0x14"}, {"form": "0x14"}],
            )

        def equipped(equipped_form=equipped_form):
            return p("Actor", "IsEquipped", [{"form": equipped_form}], "0x14")

        # Papyrus Extender evaluates real engine conditions. Restore the unused effect afterward.
        try:
            p("PO3_SKSEFunctions", "SetConditionList", [condition_form, 0, [condition]])
            stored = p("PO3_SKSEFunctions", "GetConditionList", [condition_form, 0])
            assert len(stored) == 1 and function in stored[0]
            assert not evaluate(), f"{function} matched an unequipped ring"
            for target in (0, 4, 5, 9, 6):
                menu.equip(ring_id, target)
                menu.close()
                assert evaluate(), (
                    f"{function} did not recognize the ring on finger {target}"
                )
                assert equipped(), (
                    f"Actor.IsEquipped did not recognize the ring on finger {target}"
                )
            rings.settings(iExtraRingMode=1)
            assert evaluate() and equipped(), "Cosmetic mode hid the native ring"
            rings.settings(iExtraRingMode=0)
            menu.equip(ring_id, 9)
            menu.close()
            rings.settings(iExtraRingMode=1)
            wait_for(
                lambda: not evaluate(),
                message=f"{function} retained a functional match in cosmetic mode",
            )
            assert not equipped()
            rings.settings(iExtraRingMode=0)
            wait_for(evaluate, message=f"{function} did not resume in functional mode")
            p("Actor", "UnequipAll", self_form="0x14")
            wait_for(
                lambda: not evaluate(), message=f"{function} retained a removed ring"
            )
            assert not equipped()
        finally:
            p(
                "PO3_SKSEFunctions",
                "RemoveConditionList",
                [condition_form, 0, [condition]],
            )
            assert not p("PO3_SKSEFunctions", "GetConditionList", [condition_form, 0])
    rings.checkpoint(
        f"GetEquipped, WornHasKeyword and Actor.IsEquipped follow native/virtual moves and cosmetic mode for {plugin} rings and form lists"
    )
