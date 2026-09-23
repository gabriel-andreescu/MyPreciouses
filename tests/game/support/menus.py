"""Inventory and finger-selector actions for SkyUI and vanilla menus."""

from bmk.testing import wait_for


class Inventory:
    selector = "_root.MyPreciousesFingerSelect_mc"

    def __init__(self, rings, *, vanilla=False):
        self.rings = rings
        self.p = rings.p
        self.vanilla = vanilla
        self.list = (
            "_root.Menu_mc.InventoryLists_mc.ItemsList"
            if vanilla
            else "_root.Menu_mc.inventoryLists.itemList"
        )

    def ui(self, function, path, *args, menu="InventoryMenu"):
        return self.p("UI", function, [menu, path, *args])

    def is_open(self):
        return self.p("UI", "IsMenuOpen", ["InventoryMenu"])

    def open(self):
        if not self.is_open():
            key = self.p("Input", "GetMappedKey", ["Quick Inventory", 0])
            assert key >= 0, "Quick Inventory must have a keyboard binding"
            self.rings.keyboard.tap(key)
        wait_for(
            lambda: self.ui("GetBool", "_root.Menu_mc.bFadedIn"),
            message="Inventory did not fade in",
        )
        if self.vanilla:
            root = "_root.Menu_mc.InventoryLists_mc"
            if (
                self.ui("GetInt", f"{root}.currentState") != 2
                or self.ui("GetInt", f"{self.list}.filterer.itemFilter") != 1023
            ):
                self.ui(
                    "InvokeIntA", f"{root}.CategoriesList.RestoreScrollPosition", [0, 1]
                )
                self.ui("Invoke", f"{root}.CategoriesList.UpdateList")
                self.ui("InvokeInt", "_root.Menu_mc.SelectCategory", 1)
            wait_for(
                lambda: (
                    self.ui("GetInt", f"{root}.currentState") == 2
                    and self.ui("GetInt", f"{self.list}.filterer.itemFilter") == 1023
                    and self.ui("GetInt", f"{self.list}.numUnfilteredItems") > 0
                    and not self.ui("GetBool", f"{self.list}.disableInput")
                    and not self.ui("GetBool", f"{self.list}.disableSelection")
                ),
                message="Vanilla inventory did not become ready",
            )
        else:
            # SkyUI can finish fading before its item list reaches SHOW_PANEL (1).
            wait_for(
                lambda: (
                    self.ui("GetInt", "_root.Menu_mc.inventoryLists.currentState") == 1
                    and not self.ui("GetBool", f"{self.list}.disableInput")
                    and not self.ui("GetBool", f"{self.list}.disableSelection")
                ),
                message="SkyUI inventory did not become ready",
                timeout=5,
            )

    def close(self):
        if self.is_open():
            self.ui("Invoke", f"{self.selector}.CancelSelection")
            self.ui("InvokeInt", "_root.Menu_mc.SetPlatform", 0)
            if self.vanilla:
                self.ui("Invoke", "_root.Menu_mc.onExitMenuRectClick")
            else:
                self.rings.keyboard.tap(15)
            wait_for(lambda: not self.is_open(), message="Inventory did not close")

    def select(self, form_id, name="", enchantment_id=-1, *, unique_id=None):
        self.open()
        if self.vanilla and not name:
            name = self.p("Form", "GetName", self_form=f"0x{form_id:08X}")
        # Mouse navigation reselects the hovered row when SkyUI refreshes the list.
        self.ui("InvokeInt", "_root.Menu_mc.SetPlatform", 1)
        count = self.ui("GetInt", f"{self.list}.entryList.length")
        for index in range(count):
            row = f"{self.list}.entryList.{index}"
            if (
                not self.vanilla
                and self.ui("GetInt", f"{row}.formId") & 0xFFFFFFFF != form_id
            ):
                continue
            if name and self.ui("GetString", f"{row}.myPreciousesBaseText") != name:
                continue
            if (
                unique_id is not None
                and self.ui("GetInt", f"{row}.myPreciousesCustomUniqueID") != unique_id
            ):
                continue
            if (
                enchantment_id >= 0
                and self.ui("GetInt", f"{row}.myPreciousesCustomEnchantmentID")
                & 0xFFFFFFFF
                != enchantment_id
            ):
                continue
            if self.vanilla:
                for _ in range(count):
                    selected = self.ui("GetInt", f"{self.list}.selectedIndex")
                    if selected == index:
                        break
                    move = (
                        "moveSelectionDown" if selected < index else "moveSelectionUp"
                    )
                    self.ui("Invoke", f"{self.list}.{move}")
                assert (
                    self.ui(
                        "GetString", f"{self.list}.selectedEntry.myPreciousesBaseText"
                    )
                    == name
                )
            else:
                self.ui("SetInt", f"{self.list}.selectedIndex", index)
                assert (
                    self.ui("GetInt", f"{self.list}.selectedEntry.formId") & 0xFFFFFFFF
                    == form_id
                )
            return
        raise AssertionError(f"Inventory does not contain 0x{form_id:08X} {name}")

    def open_selector(
        self, form_id, target, name="", enchantment_id=-1, *, unique_id=None
    ):
        self.select(form_id, name, enchantment_id, unique_id=unique_id)
        self.ui("InvokeInt", "_root.Menu_mc.SetPlatform", 1)
        self.ui("InvokeIntA", "_root.Menu_mc.AttemptEquip", [1 if target < 5 else 0, 0])
        self.rings.wait(
            lambda state: state["selectorOpen"], "The finger selector did not open"
        )
        row = target % 5
        wait_for(
            lambda: self.ui("GetInt", f"{self.selector}.fingerRows.{row}.targetIndex"),
            lambda actual: actual == target,
            f"Selector row {row} does not represent target {target}",
            timeout=5,
        )
        assert self.ui("GetBool", f"{self.selector}.fingerRows.{row}.enabled"), (
            f"Target {target} is disabled"
        )
        self.ui("InvokeInt", f"{self.selector}.SetSelectedIndex", row)

    def equip(self, form_id, target, name="", enchantment_id=-1, *, unique_id=None):
        from .session import assigned

        self.open_selector(form_id, target, name, enchantment_id, unique_id=unique_id)
        self.ui("Invoke", f"{self.selector}.EquipSelection")
        self.rings.wait(
            lambda state: (
                not state["selectorOpen"] and assigned(state, form_id, target)
            ),
            f"Ring did not equip on target {target}",
        )
