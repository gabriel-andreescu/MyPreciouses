"""Ring inspection, settings and actor actions used by the suite."""

import json

from bmk.skyrim.devbench import Keyboard
from bmk.testing import set_ini_values, wait_for

from .menus import Inventory


def actor(state, form_id=0x14):
    return next((item for item in state["actors"] if item["formId"] == form_id), None)


def assigned(state, form_id, target, actor_id=0x14):
    if target == 6:
        return actor(state, actor_id)["rightWorn"] == form_id
    return (
        sum(
            item["actor"] == actor_id
            and item["target"] == target
            and item["source"] == form_id
            for item in state["assignments"]
        )
        == 1
    )


class RingSession:
    def __init__(self, client, settings_path, baseline, artifacts, *, vanilla=False):
        self.client = client
        self.keyboard = Keyboard(client)
        self.p = client.papyrus
        self.call = client.call
        self.settings_path = settings_path
        self.baseline = baseline
        self.artifacts = artifacts
        self.checks = []
        (artifacts / "passed.json").write_text("[]\n", encoding="utf-8")
        self.menu = Inventory(self, vanilla=vanilla)
        self.mcm_quest = None

    def checkpoint(self, description):
        self.checks.append(description)
        (self.artifacts / "passed.json").write_text(
            json.dumps(self.checks, indent=2), encoding="utf-8"
        )

    def state(self):
        result = self.call("inspect", {"kind": "my-preciouses"})
        assert result["ok"], "MyPreciouses could not inspect the loaded player"
        return result

    def wait(self, predicate, message, timeout=10):
        return wait_for(self.state, predicate, message, timeout=timeout)

    def restore(self, name=None, cell="QASmoke"):
        self.client.load(name or self.baseline, cell=cell, settle_ms=5000)

    def save(self, name):
        self.client.save(name)
        wait_for(
            lambda: any(
                save["name"] == name
                for save in self.call("game", {"action": "list", "filter": name})[
                    "saves"
                ]
            ),
            message=f"Save {name} did not become available for loading",
        )

    def reload_settings(self):
        self.p("MyPreciouses_MCM", "OnConfigClose", self_form=self.mcm_quest)

    def settings(self, **values):
        set_ini_values(self.settings_path, values)
        self.reload_settings()

    def add(self, form_id, count=1, actor_id="0x14"):
        self.p(
            "ObjectReference",
            "AddItem",
            [{"form": f"0x{form_id:08X}"}, count, True],
            actor_id,
        )

    def npc(self, *, disabled=False, persistent=False, base_id=0xA2C8E):
        npc = self.client.spawn(
            base_id, "ACHR", disabled=disabled, persistent=persistent
        )
        self.p("Actor", "SetDontMove", [True], npc)
        self.p("Actor", "SetPlayerTeammate", [True, True], npc)
        return npc

    def shared_npc_sources(self, actors, ring, *, scripted=False):
        ids = {int(value, 16) for value in actors}

        def ready(state):
            for actor_id in ids:
                items = [
                    a
                    for a in state["assignments"]
                    if a["actor"] == actor_id and a["source"] == ring
                ]
                if len(items) != 9 or len({a["effectSource"] for a in items}) != 9:
                    return False
                if (
                    scripted
                    and sum(
                        a["actor"] == actor_id
                        and a["source"] == ring
                        and not a["suspended"]
                        for a in state["scriptBindings"]
                    )
                    != 9
                ):
                    return False
            return True

        state = self.wait(
            ready,
            "NPC rings did not retain distinct effects and script bindings per actor",
        )
        sources = sorted(
            {
                a["effectSource"]
                for a in state["assignments"]
                if a["actor"] in ids and a["source"] == ring
            }
        )
        assert len(sources) == 9, (
            f"Identical rings on {len(actors)} NPCs used {len(sources)} forms instead of nine"
        )
        return sources

    def reload_npc_cell(self, npc):
        unloaded = False
        for cell in (
            "WhiterunBreezehome",
            "WhiterunUnderforge",
            "WhiterunDragonsreachBasement",
            "WhiterunHalloftheDeadCatacombs",
            "WhiterunJorrvaskrBasement",
            "WhiterunBelethorsGeneralGoods",
            "WhiterunArcadiasCauldron",
            "WhiterunDrunkenHuntsman",
            "QASmoke",
        ):
            if unloaded and cell != "QASmoke":
                continue
            if cell == "QASmoke":
                assert unloaded, "Travel did not unload the test NPC"
            self.client.scenario(
                [
                    {"tool": "console", "args": {"command": f"coc {cell}"}},
                    {"wait": 500},
                    {"waitUntil": "noBlockingMenu", "timeoutMs": 30000},
                    {"wait": 1500},
                ]
            )
            wait_for(
                lambda cell=cell: (
                    self.call("inspect", {"kind": "scene"})["cell"]["editorId"] == cell
                ),
                message=f"The player did not reach {cell}",
            )
            if cell == "QASmoke":
                wait_for(
                    lambda: self.p("ObjectReference", "Is3DLoaded", self_form=npc),
                    message="Returning did not load NPC 3D",
                )
            else:
                unloaded = not self.p("ObjectReference", "Is3DLoaded", self_form=npc)

    def transform(self, power, race):
        self.menu.close()
        spell = {"form": f"0x{power:08X}"}
        self.p("Actor", "AddSpell", [spell, False], "0x14")
        self.p("Actor", "EquipSpell", [spell, 2], "0x14")
        key = self.p("Input", "GetMappedKey", ["Shout", 0])
        assert key >= 0, "Shout must have a keyboard binding"
        self.keyboard.tap(key)
        self.wait(
            lambda state: actor(state)["race"] == race,
            "Transformation did not change the player race",
            25,
        )

    def revert(self, quest, race):
        self.menu.close()
        # Stage 100 runs the transformation quest's return sequence.
        self.call("console", {"command": f"setstage {quest} 100"})
        self.wait(
            lambda state: actor(state)["race"] == race,
            "The transformation quest did not restore the player race",
            25,
        )
