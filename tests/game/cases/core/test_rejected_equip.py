import time

from bmk.testing import wait_for

from ...support.session import actor


def test_npc_rejected_enchantment(rings):
    p = rings.p
    rings.settings(bEnableNpcSupport=1)
    npc = rings.npc()
    npc_id = int(npc, 16)
    health = p("Actor", "GetActorValueMax", ["Health"], npc)
    rings.add(0xFCEFD, 10, npc)
    rings.wait(
        lambda s: (
            sum(a["actor"] == npc_id for a in s["assignments"]) == 9
            and actor(s, npc_id)["rightWorn"] == 0xFCEFD
        ),
        "The rejection fixture did not equip its control rings",
    )
    ring = p("Form", "TempClone", self_form="0xFCEFE")
    assert ring and ring["formId"], "The rejected ring could not be cloned"
    ring_id = int(ring["formId"], 16)
    reference = {"form": ring["formId"]}
    p("Form", "SetName", ["MyPreciouses Rejected Equip Test"], ring["formId"])
    enchantment = p("Armor", "GetEnchantment", self_form=ring["formId"])
    effect = p("Enchantment", "GetNthEffectMagicEffect", [0], enchantment["formId"])[
        "formId"
    ]
    # Skyrim rejects this worn enchantment while another enchanting effect with this flag is active.
    flag = 0x40000
    original_flag = p("MagicEffect", "IsEffectFlagSet", [flag], effect)
    try:
        p("MagicEffect", "SetEffectFlag", [flag], effect)
        rings.add(ring_id, actor_id=npc)
        time.sleep(1)
        assert actor(rings.state(), npc_id)["rightWorn"] != ring_id, (
            "The fixture did not reject the native equip"
        )
        assert p("ObjectReference", "GetItemCount", [reference], npc) == 1
        rings.checkpoint(
            "An NPC enchantment rejection leaves the game responsive and preserves the ring"
        )
        frame = rings.call("inspect", {"kind": "state"})["frame"]
        p("Actor", "OpenInventory", [True], npc)
        wait_for(
            lambda: (
                "ContainerMenu" in rings.call("menu", {"action": "list"})["openMenus"]
            ),
            message="The NPC trade menu did not open",
        )
        time.sleep(1)
        assert actor(rings.state(), npc_id)["rightWorn"] != ring_id
        assert rings.call("inspect", {"kind": "state"})["frame"] > frame
        rings.call("menu", {"action": "close", "name": "ContainerMenu"})
        wait_for(
            lambda: (
                "ContainerMenu"
                not in rings.call("menu", {"action": "list"})["openMenus"]
            ),
            message="The trade menu did not close",
        )
        rings.checkpoint(
            "Opening and closing NPC trade remains responsive after a rejected ring equip"
        )
        p("MagicEffect", "ClearEffectFlag", [flag], effect)
        p("ObjectReference", "RemoveItem", [reference, 1, True, {"form": "0x14"}], npc)
        p("ObjectReference", "RemoveItem", [reference, 1, True, {"form": npc}], "0x14")
        rings.wait(
            lambda s: actor(s, npc_id)["rightWorn"] == ring_id,
            "The NPC did not reconsider the ring after eligibility and inventory changed",
        )
        wait_for(
            lambda: p("Actor", "GetActorValueMax", ["Health"], npc) == health + 210,
            message="Reconsidering the ring lost or duplicated its enchantment bonus",
        )
        p("ObjectReference", "RemoveItem", [reference, 1, True, {"form": "0x14"}], npc)
        rings.wait(
            lambda s: actor(s, npc_id)["rightWorn"] == 0xFCEFD,
            "The NPC did not refill its native slot after the recovered ring was transferred",
        )
        rings.checkpoint(
            "Previously rejected rings remain eligible for later equip and transfer"
        )
    finally:
        p(
            "MagicEffect",
            "SetEffectFlag" if original_flag else "ClearEffectFlag",
            [flag],
            effect,
        )
        rings.call("menu", {"action": "close", "name": "ContainerMenu"})
        rings.restore()
