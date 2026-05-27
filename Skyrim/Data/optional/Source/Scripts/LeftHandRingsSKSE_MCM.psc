Scriptname LeftHandRingsSKSE_MCM extends MCM_ConfigBase

Bool Property bFixedStrengthSelected Auto Hidden
Bool Property bCosmeticExtraRingsSelected Auto Hidden

Function SyncSettings()
    Int enchantmentStrengthMode = GetModSettingInt("iEnchantmentStrengthMode:General")
    Int extraRingMode = GetModSettingInt("iExtraRingMode:General")
    bFixedStrengthSelected = enchantmentStrengthMode == 1
    bCosmeticExtraRingsSelected = extraRingMode == 1
EndFunction

Event OnConfigInit()
    SyncSettings()
EndEvent

Event OnConfigOpen()
    SyncSettings()
EndEvent

Event OnSettingChange(String a_ID)
    If a_ID == "iEnchantmentStrengthMode:General" || a_ID == "iExtraRingMode:General"
        SyncSettings()
        ForcePageReset()
    EndIf
EndEvent

Event OnConfigClose()
    OnConfigCloseNative()
EndEvent

Function OnConfigCloseNative() Native
