from ...support.session import actor


def exercise_rings(rings):
    p, menu = rings.p, rings.menu
    ring = 0x3B97C
    name = p("Form", "GetName", self_form="0x3B97C")
    p("Actor", "UnequipAll", self_form="0x14")
    rings.add(ring)
    for target in (3, 8, 3):
        menu.equip(ring, target)
        menu.close()
        state = rings.state()
        assert len(state["assignments"]) == 1 and actor(state)["rightWorn"] == 0, (
            "Moving a plain ring duplicated its assignment"
        )
        assert next(i for i in state["inventory"] if i["formId"] == ring)["count"] == 1
        menu.select(ring)
        hand, equip_state = ("L", 2) if target < 5 else ("R", 3)
        assert (
            menu.ui("GetString", f"{menu.list}.selectedEntry.text")
            == f"{name} ({hand} Ring)"
        )
        assert menu.ui("GetInt", f"{menu.list}.selectedEntry.equipState") == equip_state
        menu.close()
    rings.checkpoint(
        "A single plain ring moves between virtual fingers on opposite hands without creating another copy"
    )
    p("Actor", "UnequipAll", self_form="0x14")
    rings.add(ring, 9)
    rings.wait(
        lambda s: sum(i["formId"] == ring and i["isRing"] for i in s["inventory"]) == 1,
        "The vanilla silver ring is not recognized",
    )

    def all_fingers(state):
        player = actor(state)
        return (
            sum(a["actor"] == 0x14 for a in state["assignments"]) == 9
            and player["rightWorn"] == ring
            and sum(v["thirdPersonGeometry"] > 0 for v in player["visuals"]) == 10
        )

    for target in range(10):
        menu.equip(ring, target)
    menu.close()
    rings.wait(
        all_fingers,
        "Ten rings did not produce nine virtual models and the vanilla ring",
    )
    menu.select(ring)
    label = f"{name} (L Thumb, L Index, L Middle, L Ring, L Pinky, R Thumb, R Index, R Middle, R Ring, R Pinky)"
    assert menu.ui("GetString", f"{menu.list}.selectedEntry.text") == label
    assert menu.ui("GetInt", f"{menu.list}.selectedEntry.equipState") == 4
    menu.close()
    rings.checkpoint("Equip all ten fingers with third-person geometry")
    p("Game", "ForceFirstPerson")
    p("Actor", "DrawWeapon", self_form="0x14")
    rings.wait(
        lambda s: sum(v["firstPersonGeometry"] > 0 for v in actor(s)["visuals"]) == 10,
        "First-person ring models did not attach",
    )
    rings.checkpoint("First-person geometry for all ten fingers")
    menu.open_selector(ring, 0)
    before = rings.state()["assignments"]
    menu.ui("Invoke", f"{menu.selector}.CancelSelection")
    rings.wait(lambda s: not s["selectorOpen"], "Cancel did not close the selector")
    assert rings.state()["assignments"] == before
    menu.close()
    rings.checkpoint("Selector cancellation preserves assignments")
    save = "MyPreciousesTest_AllFingers"
    rings.save(save)
    p("Actor", "UnequipAll", self_form="0x14")
    rings.wait(
        lambda s: not s["assignments"] and actor(s)["rightWorn"] == 0,
        "UnequipAll retained rings",
    )
    menu.select(ring)
    assert menu.ui("GetString", f"{menu.list}.selectedEntry.text") == name
    assert menu.ui("GetInt", f"{menu.list}.selectedEntry.equipState") == 0
    menu.close()
    rings.checkpoint("UnequipAll clears all ten fingers")
    rings.restore(save)
    rings.wait(all_fingers, "Save/load did not restore rings and models")
    rings.checkpoint("Save/load restores assignments and visuals")


def test_all_fingers(rings):
    exercise_rings(rings)
