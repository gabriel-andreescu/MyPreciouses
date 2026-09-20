import time

import pytest
from bmk.testing import wait_for

from ...support.session import assigned
from ..core.test_custom_identity import exercise_custom_identity
from ..core.test_favorites import exercise_favorites
from ..core.test_rings import exercise_rings

pytestmark = pytest.mark.vanilla


def test_ordinary_inventory_names(rings):
    menu = rings.menu
    menu.open()
    count = menu.ui("GetInt", f"{menu.list}.entryList.length")
    assert count > 0, "The baseline must contain ordinary inventory items"
    for index in range(count):
        text = menu.ui("GetString", f"{menu.list}.entryList.{index}.text")
        assert text and text.strip() and text != "undefined", (
            "An ordinary inventory row lost its name"
        )
    menu.close()
    rings.checkpoint("Vanilla inventory retains ordinary item names without any rings")


def test_all_fingers(rings):
    exercise_rings(rings)


def test_custom_identity(rings):
    exercise_custom_identity(rings)


def test_favorites(rings):
    exercise_favorites(rings)


@pytest.mark.buffered_input
def test_modifier_hints_and_buffered_keyboard(rings):
    p, menu = rings.p, rings.menu
    rings.settings(
        bAlwaysChooseFinger=0,
        iFingerSelectModifierKey=42,
        iFingerSelectModifierButton=275,
    )
    p("Actor", "UnequipAll", self_form="0x14")
    rings.add(0x3B97C)
    menu.select(0x3B97C)
    for platform in (0, 1, 0):
        menu.ui("InvokeInt", "_root.Menu_mc.SetPlatform", platform)
        menu.ui("Invoke", "_root.Menu_mc.UpdateBottomBarButtons")
        bar = "_root.Menu_mc.BottomBar_mc.Buttons"
        assert menu.ui("GetString", f"{bar}.0.label") == "Finger"
        assert menu.ui("GetBool", f"{bar}.0._visible")
        if platform == 0:
            assert menu.ui("GetString", f"{bar}.0.PCButton") == "L-Shift"
        else:
            assert menu.ui("GetString", f"{bar}.0.XBoxButton") == "360_RB"
        for index, label in ((1, "$Equip"), (2, "$Drop"), (3, "$Favorite")):
            assert menu.ui("GetString", f"{bar}.{index}.label") == label
    rings.checkpoint(
        "Vanilla inventory shows Shift and RB finger hints while preserving its other actions"
    )
    menu.ui("InvokeIntA", "_root.Menu_mc.AttemptEquip", [1, 0])
    rings.wait(
        lambda s: not s["selectorOpen"] and assigned(s, 0x3B97C, 1),
        "Equipping without Shift did not use the left index",
    )

    def shift_select():
        # Modifier detection reads DirectInput state, not dispatched key events.
        try:
            p("Input", "HoldKey", [42])
            time.sleep(0.1)
            menu.ui("InvokeIntA", "_root.Menu_mc.AttemptEquip", [1, 0])
            rings.wait(
                lambda s: s["selectorOpen"], "Shift did not open the finger selector"
            )
        finally:
            p("Input", "ReleaseKey", [42])

    shift_select()
    menu.ui("InvokeInt", f"{menu.selector}.SetPlatform", 1)
    assert menu.ui("GetString", f"{menu.selector}.EquipButton.XBoxButton") == "360_A"
    assert menu.ui("GetString", f"{menu.selector}.CancelButton.XBoxButton") == "360_B"
    menu.ui("InvokeInt", f"{menu.selector}.SetPlatform", 0)
    before = menu.ui("GetInt", f"{menu.selector}.selectedRowIndex")
    p("Input", "TapKey", [208])
    wait_for(
        lambda: menu.ui("GetInt", f"{menu.selector}.selectedRowIndex") == before + 1,
        message="The down key did not move finger selection",
    )
    target = menu.ui("GetInt", f"{menu.selector}.fingerRows.{before + 1}.targetIndex")
    p("Input", "TapKey", [18])
    rings.wait(
        lambda s: not s["selectorOpen"] and assigned(s, 0x3B97C, target),
        "The E key did not equip the selected finger",
    )
    rings.checkpoint(
        "Shift opens vanilla finger selection and keyboard navigation and E equip the selected ring"
    )
    for cancel in (15, 1):
        menu.select(0x3B97C)
        assignments = rings.state()["assignments"]
        shift_select()
        p("Input", "TapKey", [cancel])
        rings.wait(
            lambda s: not s["selectorOpen"],
            "The cancel key did not dismiss finger selection",
        )
        assert menu.is_open(), "Cancelling finger selection also closed inventory"
        assert rings.state()["assignments"] == assignments
    rings.checkpoint(
        "Tab and Escape cancel finger selection without closing vanilla inventory or changing equipment"
    )
    menu.close()


def test_selector_cancel_input(rings):
    menu = rings.menu
    rings.settings(bAlwaysChooseFinger=1)
    rings.p("Actor", "UnequipAll", self_form="0x14")
    rings.add(0x3B97C)
    for cancel in (15, 1):
        menu.open_selector(0x3B97C, 0)
        assignments = rings.state()["assignments"]
        rings.keyboard.tap(cancel)
        rings.wait(
            lambda s: not s["selectorOpen"], "Cancel did not close finger selection"
        )
        assert menu.is_open(), "Cancelling finger selection also closed inventory"
        assert rings.state()["assignments"] == assignments
    menu.close()
    rings.checkpoint(
        "Native Tab and Escape cancel selection without closing inventory or changing rings"
    )
