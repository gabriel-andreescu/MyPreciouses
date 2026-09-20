from bmk.testing import wait_for

from ...support.session import assigned


def exercise_favorites(rings):
    p, menu = rings.p, rings.menu
    rings.restore()
    p("Actor", "UnequipAll", self_form="0x14")
    rings.settings(bAlwaysChooseFinger=1, iExtraRingMode=0, iEnchantmentStrengthMode=0)
    ring = 0xFCEFD
    rings.add(ring, 2)
    menu.equip(ring, 1)
    menu.select(ring)
    rings.keyboard.tap(33)
    menu.close()
    key = p("Input", "GetMappedKey", ["Favorites", 0])
    assert key >= 0, "Favorites must have a keyboard binding"
    rings.keyboard.tap(key)
    favorite_list = "_root.MenuHolder.Menu_mc.itemList"

    def ui(function, path, *args):
        return p("UI", function, ["FavoritesMenu", path, *args])

    def confirm():
        # Both menus send ItemSelect without a hand argument for keyboard confirmation.
        ui(
            "InvokeInt" if menu.vanilla else "InvokeIntA",
            f"{favorite_list}.onItemPress",
            1 if menu.vanilla else [0, 1],
        )

    wait_for(
        lambda: ui("GetInt", f"{favorite_list}.entryList.length") == 1,
        message="The baseline must have no other favorites",
    )
    ui("SetInt", f"{favorite_list}.selectedIndex", 0)
    if menu.vanilla:
        name = p("Form", "GetName", self_form="0xFCEFD")
        assert ui("GetString", f"{favorite_list}.selectedEntry.text") == f"{name} (2)"
    else:
        assert ui("GetInt", f"{favorite_list}.selectedEntry.formId") == ring
    confirm()
    rings.wait(
        lambda s: s["selectorOpen"],
        "Keyboard confirmation in Favorites bypassed finger selection",
    )
    wait_for(
        lambda: ui("GetInt", f"{menu.selector}.fingerRows.2.targetIndex") == 7,
        message="Favorites did not show the right-hand selector",
    )
    ui("InvokeInt", f"{menu.selector}.SetSelectedIndex", 2)
    ui("Invoke", f"{menu.selector}.EquipSelection")
    rings.wait(
        lambda s: (
            not s["selectorOpen"] and assigned(s, ring, 1) and assigned(s, ring, 7)
        ),
        "Favorites did not equip a second copy on the chosen finger",
    )
    rings.checkpoint(
        "Keyboard confirmation in Favorites selects a finger and preserves the other ring copy"
    )
    before = rings.state()["assignments"]
    confirm()
    rings.wait(lambda s: s["selectorOpen"], "Favorites did not reopen the selector")
    ui("Invoke", f"{menu.selector}.CancelSelection")
    rings.wait(lambda s: not s["selectorOpen"], "Favorites did not cancel the selector")
    assert rings.state()["assignments"] == before
    confirm()
    rings.wait(
        lambda s: s["selectorOpen"],
        "Favorites input did not recover after cancellation",
    )
    rings.call("menu", {"action": "close", "name": "FavoritesMenu"})
    rings.wait(
        lambda s: not s["selectorOpen"],
        "Closing Favorites retained its selector session",
    )
    assert rings.state()["assignments"] == before
    rings.checkpoint(
        "Favorites cancellation and host closure release the selector without changing equipment"
    )


def test_favorites(rings):
    exercise_favorites(rings)
