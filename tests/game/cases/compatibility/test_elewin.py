import pytest
from bmk.testing import wait_for


@pytest.mark.compatibility("Elewin")
def test_navel_piercings_are_not_rings(rings):
    p, menu = rings.p, rings.menu
    p("Actor", "UnequipAll", self_form="0x14")
    for local_id in (0xD64, 0xD65, 0xD69, 0xD6A, 0xD6B):
        item = rings.client.form_id(local_id, "Elewin Jewelry.esp")
        rings.add(item)
        entries = [i for i in rings.state()["inventory"] if i["formId"] == item]
        assert (
            len(entries) == 1
            and not entries[0]["isRing"]
            and not entries[0]["hasRingModel"]
        )
        menu.select(item)
        menu.ui("InvokeInt", "_root.Menu_mc.SetPlatform", 1)
        menu.ui("InvokeIntA", "_root.Menu_mc.AttemptEquip", [0, 0])
        wait_for(
            lambda item=item: p(
                "Actor", "IsEquipped", [{"form": f"0x{item:08X}"}], "0x14"
            ),
            message="The navel piercing did not equip normally",
        )
        state = rings.state()
        assert not state["selectorOpen"] and not state["assignments"]
        menu.close()
        p("Actor", "UnequipAll", self_form="0x14")
    rings.checkpoint(
        "Elewin navel piercings equip normally without entering finger selection"
    )
