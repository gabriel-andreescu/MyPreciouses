import time

import pytest

from ...support.session import actor, assigned


@pytest.mark.compatibility("Mysticism")
def test_ghostwalk_ring_models(rings):
    p, menu = rings.p, rings.menu
    p("Actor", "UnequipAll", self_form="0x14")
    spell = f"0x{rings.client.form_id(0x2CA226, 'MysticismMagic.esp'):08X}"
    invisibility = rings.client.form_id(0xB64E08, "MysticismMagic.esp")
    rings.add(0x3B97C, 10)
    rings.add(0x877C9)
    for target in range(10):
        menu.equip(0x3B97C, target)
    menu.close()
    p("Game", "ForceFirstPerson")
    p("Actor", "DrawWeapon", self_form="0x14")

    def models(state, invisible):
        current = actor(state)
        active = any(
            e["effect"] == invisibility and not e["inactive"] and not e["dispelled"]
            for e in current["effects"]
        )
        if active != invisible:
            return False
        for view in ("firstPerson", "thirdPerson"):
            if sum(v[f"{view}Geometry"] > 0 for v in current["visuals"]) != 10:
                return False
            for visual in current["visuals"]:
                shaders = visual[f"{view}Shaders"]
                if not shaders or any(
                    shader["temporaryRefraction"] != invisible for shader in shaders
                ):
                    return False
        return True

    rings.wait(
        lambda s: models(s, False), "The control rings are missing or already refracted"
    )
    p("Spell", "Cast", [{"form": "0x14"}, {"form": "0x14"}], spell)
    rings.wait(
        lambda s: models(s, True),
        "Ghostwalk did not hide all ten existing rings in both views",
    )
    rings.checkpoint("Ghostwalk hides all ten existing rings in first and third person")
    # Replacing one ring rebuilds the virtual attachments while invisibility is active.
    menu.equip(0x877C9, 0)
    menu.close()
    for sample in range(1, 4):
        time.sleep(1)
        state = rings.state()
        assert assigned(state, 0x877C9, 0)
        assert models(state, True), (
            f"Rebuilt rings lost Ghostwalk refraction at sample {sample}"
        )
    rings.checkpoint(
        "Replacing a ring during Ghostwalk preserves invisibility on every rebuilt model"
    )
    rings.wait(
        lambda s: models(s, False),
        "Rings did not become visible after Ghostwalk expired",
        60,
    )
    rings.checkpoint("Ghostwalk expiration restores all ten rings in both views")
