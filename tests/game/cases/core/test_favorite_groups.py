import pytest
from bmk.testing import wait_for

from ...support.session import actor, assigned


class FavoriteGroups:
    root = "_root.MenuHolder.Menu_mc"

    def __init__(self, rings):
        self.rings = rings

    def manager(self, function, *args):
        return self.rings.p(
            "SKI_FavoritesManager",
            function,
            list(args),
            self_form="SKI_FavoritesManagerInstance",
        )

    def ui(self, function, path, *args):
        return self.rings.p(
            "UI", function, ["FavoritesMenu", f"{self.root}.{path}", *args]
        )

    def favorite(self, form, *, spell=False, unique_id=None):
        rings = self.rings
        if not spell:
            rings.menu.select(form, unique_id=unique_id)
        else:
            rings.menu.close()
            key = rings.p("Input", "GetMappedKey", ["Quick Magic", 0])
            rings.keyboard.tap(key)
            root = "_root.Menu_mc.inventoryLists.itemList"

            def ui(function, path, *args):
                return rings.p("UI", function, ["MagicMenu", path, *args])

            wait_for(
                lambda: ui("GetBool", "_root.Menu_mc.bFadedIn"),
                message="Magic menu did not fade in",
            )
            ui("InvokeInt", "_root.Menu_mc.SetPlatform", 1)
            count = ui("GetInt", f"{root}.entryList.length")
            index = next(
                i
                for i in range(count)
                if ui("GetInt", f"{root}.entryList.{i}.formId") & 0xFFFFFFFF == form
            )
            ui("SetInt", f"{root}.selectedIndex", index)
            assert ui("GetInt", f"{root}.selectedEntry.formId") & 0xFFFFFFFF == form
        favorited = rings.p("Game", "IsObjectFavorited", [{"form": hex(form)}])
        if unique_id is not None:
            flag = rings.menu.ui(
                "GetInt", "_root.Menu_mc.inventoryLists.categoryList.entryList.0.flag"
            )
            favorited = bool(
                rings.menu.ui("GetInt", f"{rings.menu.list}.selectedEntry.filterFlag")
                & flag
            )
        if not favorited:
            rings.keyboard.tap(33)
        wait_for(
            lambda: rings.p("Game", "IsObjectFavorited", [{"form": hex(form)}]),
            message=f"{form:08X} was not favorited",
        )
        if spell:
            rings.call("menu", {"action": "close", "name": "MagicMenu"})
        else:
            rings.menu.close()

    def open(self):
        self.rings.call("menu", {"action": "open", "name": "FavoritesMenu"})
        wait_for(
            lambda: self.ui("GetInt", "itemList.entryList.length") > 0,
            message="Favorites did not populate",
        )

    def close(self):
        self.rings.call("menu", {"action": "close", "name": "FavoritesMenu"})
        wait_for(
            lambda: not self.rings.p("UI", "IsMenuOpen", ["FavoritesMenu"]),
            message="Favorites did not close",
        )

    def rows(self, form):
        count = self.ui("GetInt", "itemList.entryList.length")
        return [
            f"itemList.entryList.{i}"
            for i in range(count)
            if self.ui("GetInt", f"itemList.entryList.{i}.formId") & 0xFFFFFFFF == form
        ]

    def add(self, group, *forms):
        self.open()
        for form in forms:
            item_ids = {self.ui("GetInt", f"{row}.itemId") for row in self.rows(form)}
            assert item_ids
            for item_id in item_ids:
                assert self.manager("GroupAdd", group, item_id, {"form": hex(form)})
        self.close()

    def save(self, group):
        self.open()
        self.ui("SetBool", "_groupButtonFocused", True)
        self.ui("SetInt", "_groupIndex", group)
        self.ui("Invoke", "startSaveEquipState")
        wait_for(
            lambda: self.ui("GetInt", "_state") == 0,
            message="SkyUI did not finish saving group equipment",
        )

    def flag(self, group, flag, enabled):
        self.manager("SetGroupFlag", group, flag, enabled)

    def use(self, group):
        keys = self.manager("GetGroupHotkeys")
        self.rings.keyboard.tap(keys[group])
        wait_for(
            lambda: self.manager("GetState") == "",
            message="SkyUI did not finish applying the group",
        )

    def marker(self, form, member, group=0):
        return any(
            self.ui("GetInt", f"{row}.{member}") & (1 << group)
            for row in self.rows(form)
        )


@pytest.mark.parametrize("unequip_armor", [False, True])
def test_group_rings_and_spells(rings, unequip_armor):
    """Saving rings must not overwrite spells, and undress/outfit groups restore both."""
    p, menu = rings.p, rings.menu
    groups = FavoriteGroups(rings)
    right, left, helmet = 0x1CF2B, 0xFCEFD, 0x12E4D
    flames, healing = 0x12FCD, 0x12FCC
    p("Actor", "UnequipAll", self_form="0x14")
    for form in (right, left, helmet):
        rings.add(form)
        groups.favorite(form)
    for spell, hand in ((flames, 1), (healing, 0)):
        p("Actor", "AddSpell", [{"form": hex(spell)}, False], "0x14")
        p("Actor", "EquipSpell", [{"form": hex(spell)}, hand], "0x14")
        groups.favorite(spell, spell=True)
    menu.equip(right, 6)
    menu.equip(left, 1)
    menu.close()
    p("Actor", "EquipItem", [{"form": hex(helmet)}, False, True], "0x14")
    groups.add(0, right, left, helmet, flames, healing)
    groups.save(0)
    assert groups.marker(right, "mainHandFlag")
    assert groups.marker(left, "offHandFlag")
    assert groups.marker(flames, "mainHandFlag")
    assert groups.marker(healing, "offHandFlag")
    groups.close()
    groups.open()
    assert groups.marker(right, "mainHandFlag")
    assert groups.marker(flames, "mainHandFlag")
    groups.close()
    rings.checkpoint(
        "Rings and spells retain independent saved hand markers after reopening Favorites"
    )
    groups.flag(1, 1, True)
    groups.flag(1, 2, True)
    groups.use(1)
    rings.wait(
        lambda s: not assigned(s, right, 6) and not assigned(s, left, 1),
        "The empty undress group left a ring equipped",
    )
    for hand in (0, 1):
        assert p("Actor", "GetEquippedObject", [hand], "0x14") is None
    groups.flag(0, 1, unequip_armor)
    groups.use(0)
    rings.wait(
        lambda s: assigned(s, right, 6) and assigned(s, left, 1),
        "The outfit group did not restore each ring to its saved finger",
    )
    for spell, hand in ((flames, 1), (healing, 0)):
        assert (
            int(p("Actor", "GetEquippedObject", [hand], "0x14")["formId"], 16) == spell
        )
    assert p("Actor", "IsEquipped", [{"form": hex(helmet)}], "0x14")
    rings.checkpoint(
        "Empty undress and outfit hotkeys restore armor, both rings and both spells"
    )
    before = rings.state()["assignments"]
    groups.use(0)
    assert rings.state()["assignments"] == before
    rings.save("MyPreciousesTest_FavoriteGroups")
    rings.restore("MyPreciousesTest_FavoriteGroups")
    groups.use(1)
    groups.use(0)
    rings.wait(
        lambda s: assigned(s, right, 6) and assigned(s, left, 1),
        "Saved groups lost their ring positions after loading",
    )
    for spell, hand in ((flames, 1), (healing, 0)):
        assert (
            int(p("Actor", "GetEquippedObject", [hand], "0x14")["formId"], 16) == spell
        )
    rings.checkpoint("Repeated activation and save/load preserve the outfit and spells")
    menu.equip(right, 1)
    menu.equip(left, 6)
    menu.close()
    groups.add(2, right, left, helmet, flames, healing)
    groups.save(2)
    groups.close()
    for group, native, virtual in ((0, right, left), (2, left, right)):
        groups.use(group)
        rings.wait(
            lambda s, native=native, virtual=virtual: (
                assigned(s, native, 6) and assigned(s, virtual, 1)
            ),
            "Switching groups failed to swap native and virtual rings",
        )
    rings.checkpoint(
        "Switching groups swaps the native and virtual rings without losing either assignment"
    )
    groups.save(3)
    groups.close()
    groups.add(3, right)
    groups.flag(3, 1, True)
    groups.use(3)
    rings.wait(
        lambda s: assigned(s, right, 6) and not s["assignments"],
        "A newly added ring without a saved finger did not equip normally",
    )
    rings.checkpoint(
        "New group members without saved fingers retain SkyUI's normal equip behavior"
    )


@pytest.mark.parametrize("custom", [False, True])
def test_group_identical_virtual_rings(rings, custom):
    """A group's row can own two copies without using the native ring slot."""
    p, menu = rings.p, rings.menu
    groups = FavoriteGroups(rings)
    ring = 0x3B97C if custom else 0xFCEFD
    p("Actor", "UnequipAll", self_form="0x14")
    health = p("Actor", "GetActorValueMax", ["Health"], "0x14")
    if custom:
        p("Actor", "ForceActorValue", ["Enchanting", 30.0], "0x14")
        for count in (1, 2):
            rings.call("console", {"command": "playerenchantobject 3b97c 493aa"})
            rings.wait(
                lambda s, count=count: any(
                    i["formId"] == ring and i["count"] == count for i in s["inventory"]
                ),
                "Custom ring creation did not complete",
            )
    else:
        rings.add(ring, 2)
    # The first equip gives its copy an ID. The other custom copy remains unbound.
    identities = [None, 0 if custom else None]
    for identity, target in zip(identities, (1, 7), strict=True):
        groups.favorite(ring, unique_id=identity)
        menu.equip(ring, target, unique_id=identity)
    menu.close()
    expected = {
        item["target"]: item["uniqueId"] for item in rings.state()["assignments"]
    }
    assert len(set(expected.values())) == 2
    if custom:
        identities = [expected[1], expected[7]]
    groups.add(0, ring)
    groups.save(0)
    assert groups.marker(ring, "mainHandFlag")
    assert groups.marker(ring, "offHandFlag")
    groups.close()
    p("Actor", "UnequipAll", self_form="0x14")
    for identity, target in zip(identities, (3, 8), strict=True):
        menu.equip(ring, target, unique_id=identity)
    menu.close()
    groups.add(2, ring)
    groups.save(2)
    groups.close()
    for group, targets in ((0, {1, 7}), (2, {3, 8}), (0, {1, 7})):
        groups.use(group)
        state = rings.wait(
            lambda s, targets=targets: (
                {a["target"] for a in s["assignments"]} == targets
            ),
            "Switching groups failed to move both identical copies",
        )
        assert actor(state)["rightWorn"] == 0
        if group == 0:
            assert {
                a["target"]: a["uniqueId"] for a in state["assignments"]
            } == expected
        wait_for(
            lambda: (
                p("Actor", "GetActorValueMax", ["Health"], "0x14")
                == health + (60 if custom else 40)
            ),
            message="Switching groups changed the combined ring bonus",
        )
    before = rings.state()["assignments"]
    groups.use(0)
    assert rings.state()["assignments"] == before
    groups.flag(3, 2, True)
    groups.use(3)
    assert rings.state()["assignments"] == before
    rings.checkpoint(
        "Groups move both identical virtual copies without toggling them or occupying the native slot"
    )
    groups.flag(1, 1, True)
    groups.use(1)
    rings.wait(
        lambda s: not s["assignments"], "Unequip Armor left virtual rings equipped"
    )
    for _ in range(2):
        if not p("Game", "IsObjectFavorited", [{"form": hex(ring)}]):
            break
        p("PO3_SKSEFunctions", "UnmarkItemAsFavorite", [{"form": hex(ring)}])
    assert not p("Game", "IsObjectFavorited", [{"form": hex(ring)}])
    groups.use(0)
    assert not rings.state()["assignments"]
    groups.favorite(ring)
    p("ObjectReference", "RemoveItem", [{"form": hex(ring)}, 1, True], "0x14")
    groups.favorite(ring)
    groups.open()
    groups.close()
    groups.use(0)
    state = rings.wait(
        lambda s: len(s["assignments"]) == 1,
        "A missing copy prevented the remaining copy from equipping",
    )
    remaining = state["assignments"][0]
    assert expected[remaining["target"]] == remaining["uniqueId"]
    assert actor(state)["rightWorn"] == 0
    rings.checkpoint(
        "Missing copies are skipped without moving the remaining copy to another saved finger"
    )
    groups.open()
    item_id = groups.ui("GetInt", f"{groups.rows(ring)[0]}.itemId")
    groups.close()
    groups.manager("GroupRemove", 0, item_id)
    groups.flag(0, 1, True)
    groups.use(0)
    rings.wait(lambda s: not s["assignments"], "A removed group member was restored")
    assert actor(rings.state())["rightWorn"] == 0
    rings.checkpoint("Removing a group member clears all saved placements for that row")


def test_group_scripted_ring(rings):
    """Repeated activation must not replay a scripted ring's equip event."""
    p, menu = rings.p, rings.menu
    groups = FavoriteGroups(rings)
    namira = 0x2C37B
    perk = {"form": "0xEE5C3"}
    p("Actor", "UnequipAll", self_form="0x14")
    assert not p("Actor", "HasPerk", [perk], "0x14")
    rings.add(namira)
    groups.favorite(namira)
    menu.equip(namira, 0)
    menu.close()
    wait_for(
        lambda: p("Actor", "HasPerk", [perk], "0x14"),
        message="Namira did not grant its perk",
    )
    groups.add(0, namira)
    groups.save(0)
    groups.close()
    p("Actor", "RemovePerk", [perk], "0x14")
    before = rings.state()["scriptBindings"]
    groups.use(0)
    assert not p("Actor", "HasPerk", [perk], "0x14")
    assert rings.state()["scriptBindings"] == before
    groups.flag(1, 1, True)
    groups.use(1)
    rings.wait(
        lambda s: not s["scriptBindings"],
        "The undress group retained Namira's script binding",
    )
    groups.use(0)
    wait_for(
        lambda: p("Actor", "HasPerk", [perk], "0x14"),
        message="The outfit group did not restore Namira's perk",
    )
    groups.use(1)
    wait_for(
        lambda: not p("Actor", "HasPerk", [perk], "0x14"),
        message="The undress group retained Namira's perk",
    )
    rings.checkpoint(
        "Repeated group activation preserves script state, while unequip and restore run the ring's events"
    )
