Scriptname LeftHandRingsSKSE_MCM extends MCM_ConfigBase

Bool Property bFixedStrengthSelected Auto Hidden
Bool Property bCosmeticExtraRingsSelected Auto Hidden
Bool Property bControllerInputSelected Auto Hidden

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

Event OnConfigInit()
    SyncSettings()
EndEvent

Event OnConfigOpen()
    SyncSettings()
EndEvent

Event OnSettingChange(String a_ID)
    If a_ID == "iFingerSelectModifierButton:General" && Game.UsingGamepad()
        Int button = GetModSettingInt("iFingerSelectModifierButton:General")
        If ShouldShowVanillaControllerHintWarning(button)
            ShowMessage("Vanilla UI inventory hints only support RB. The finger selector still works, but the inventory hint will not be shown for this button.")
        EndIf
    EndIf

    If SyncSettings()
        ForcePageReset()
    EndIf
EndEvent

Event OnConfigClose()
    OnConfigCloseNative()
EndEvent

Function OnConfigCloseNative() Native

Bool Function ShouldShowVanillaControllerHintWarning(Int aiButton) Native
