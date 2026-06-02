Scriptname LeftHandRingsSKSE_MCM extends MCM_ConfigBase

Bool Property bFixedStrengthSelected Auto Hidden
Bool Property bCosmeticExtraRingsSelected Auto Hidden
Bool Property bControllerInputSelected Auto Hidden
Bool Property bRightIndexSlotConfigurable = False Auto Hidden
Bool Property bRightIndexSlotDisplayed = True Auto Hidden
Int Property VirtualSlotPreset Auto Hidden

Bool Function SyncVirtualSlotDisplay()
    Bool changed = False

    If bRightIndexSlotConfigurable
        bRightIndexSlotConfigurable = False
        changed = True
    EndIf

    If !bRightIndexSlotDisplayed
        bRightIndexSlotDisplayed = True
        changed = True
    EndIf

    Return changed
EndFunction

Bool Function SyncSettings()
    Bool wasFixedStrengthSelected = bFixedStrengthSelected
    Bool wasCosmeticExtraRingsSelected = bCosmeticExtraRingsSelected
    Bool wasControllerInputSelected = bControllerInputSelected

    Int enchantmentStrengthMode = GetModSettingInt("iEnchantmentStrengthMode:General")
    Int extraRingMode = GetModSettingInt("iExtraRingMode:General")
    bFixedStrengthSelected = enchantmentStrengthMode == 1
    bCosmeticExtraRingsSelected = extraRingMode == 1
    bControllerInputSelected = Game.UsingGamepad()

    Bool fixedStrengthChanged = bFixedStrengthSelected != wasFixedStrengthSelected
    Bool extraRingModeChanged = bCosmeticExtraRingsSelected != wasCosmeticExtraRingsSelected
    Bool inputDeviceChanged = bControllerInputSelected != wasControllerInputSelected
    Return fixedStrengthChanged || extraRingModeChanged || inputDeviceChanged
EndFunction

Bool Function SyncVirtualSlotPreset()
    Int preset = GetVirtualSlotPreset()
    If VirtualSlotPreset != preset
        VirtualSlotPreset = preset
        Return True
    EndIf

    Return False
EndFunction

Int Function GetVirtualSlotPreset()
    Bool leftThumb = GetModSettingBool("bEnableLeftThumb:VirtualSlots")
    Bool leftIndex = GetModSettingBool("bEnableLeftIndex:VirtualSlots")
    Bool leftMiddle = GetModSettingBool("bEnableLeftMiddle:VirtualSlots")
    Bool leftRing = GetModSettingBool("bEnableLeftRing:VirtualSlots")
    Bool leftPinky = GetModSettingBool("bEnableLeftPinky:VirtualSlots")
    Bool rightThumb = GetModSettingBool("bEnableRightThumb:VirtualSlots")
    Bool rightMiddle = GetModSettingBool("bEnableRightMiddle:VirtualSlots")
    Bool rightRing = GetModSettingBool("bEnableRightRing:VirtualSlots")
    Bool rightPinky = GetModSettingBool("bEnableRightPinky:VirtualSlots")

    If leftThumb && leftIndex && leftMiddle && leftRing && leftPinky && rightThumb && rightMiddle && rightRing && rightPinky
        Return 0
    EndIf

    If !leftThumb && leftIndex && !leftMiddle && !leftRing && !leftPinky && !rightThumb && !rightMiddle && !rightRing && !rightPinky
        Return 1
    EndIf

    Return 2
EndFunction

Bool Function IsVirtualSlotSetting(String a_ID)
    If a_ID == "bEnableLeftThumb:VirtualSlots"
        Return True
    ElseIf a_ID == "bEnableLeftIndex:VirtualSlots"
        Return True
    ElseIf a_ID == "bEnableLeftMiddle:VirtualSlots"
        Return True
    ElseIf a_ID == "bEnableLeftRing:VirtualSlots"
        Return True
    ElseIf a_ID == "bEnableLeftPinky:VirtualSlots"
        Return True
    ElseIf a_ID == "bEnableRightThumb:VirtualSlots"
        Return True
    ElseIf a_ID == "bEnableRightMiddle:VirtualSlots"
        Return True
    ElseIf a_ID == "bEnableRightRing:VirtualSlots"
        Return True
    ElseIf a_ID == "bEnableRightPinky:VirtualSlots"
        Return True
    EndIf

    Return False
EndFunction

Function SetVirtualSlots(Bool abLeftThumb, Bool abLeftIndex, Bool abLeftMiddle, Bool abLeftRing, Bool abLeftPinky, Bool abRightThumb, Bool abRightMiddle, Bool abRightRing, Bool abRightPinky)
    SetModSettingBool("bEnableLeftThumb:VirtualSlots", abLeftThumb)
    SetModSettingBool("bEnableLeftIndex:VirtualSlots", abLeftIndex)
    SetModSettingBool("bEnableLeftMiddle:VirtualSlots", abLeftMiddle)
    SetModSettingBool("bEnableLeftRing:VirtualSlots", abLeftRing)
    SetModSettingBool("bEnableLeftPinky:VirtualSlots", abLeftPinky)
    SetModSettingBool("bEnableRightThumb:VirtualSlots", abRightThumb)
    SetModSettingBool("bEnableRightMiddle:VirtualSlots", abRightMiddle)
    SetModSettingBool("bEnableRightRing:VirtualSlots", abRightRing)
    SetModSettingBool("bEnableRightPinky:VirtualSlots", abRightPinky)
EndFunction

Bool Function ApplyVirtualSlotPreset(Int aiPreset)
    VirtualSlotPreset = aiPreset

    If aiPreset == 0
        SetVirtualSlots(True, True, True, True, True, True, True, True, True)
        ForcePageReset()
        Return True
    ElseIf aiPreset == 1
        SetVirtualSlots(False, True, False, False, False, False, False, False, False)
        ForcePageReset()
        Return True
    EndIf

    Return False
EndFunction

Event OnConfigInit()
    SyncVirtualSlotDisplay()
    SyncVirtualSlotPreset()
    SyncSettings()
EndEvent

Event OnConfigOpen()
    SyncVirtualSlotDisplay()
    SyncVirtualSlotPreset()
    SyncSettings()
EndEvent

Event OnSettingChange(String a_ID)
    Bool pageNeedsReset = False

    If a_ID == "iFingerSelectModifierButton:General" && Game.UsingGamepad()
        Int button = GetModSettingInt("iFingerSelectModifierButton:General")
        If ShouldShowVanillaControllerHintWarning(button)
            ShowMessage("Vanilla UI inventory hints only support RB. The finger selector still works, but the inventory hint will not be shown for this button.")
        EndIf
    EndIf

    If IsVirtualSlotSetting(a_ID)
        pageNeedsReset = SyncVirtualSlotPreset()
    EndIf

    If SyncSettings() || pageNeedsReset
        ForcePageReset()
    EndIf
EndEvent

Event OnConfigClose()
    OnConfigCloseNative()
EndEvent

Function OnConfigCloseNative() Native

Bool Function ShouldShowVanillaControllerHintWarning(Int aiButton) Native
