Scriptname LeftHandRingsSKSE_MCM extends MCM_ConfigBase

Event OnConfigClose() Native

String Property EnableBondSetting = "bEnableBondOfMatrimony:General" AutoReadOnly
String Property NormalSlotSetting = "sBipedSlot:General" AutoReadOnly
String Property BondSlotSetting = "sBondBipedSlot:General" AutoReadOnly
String Property DefaultBondSlot = "Arm Right / Primary Arm (59)" AutoReadOnly

Bool _refreshingSlotMenus = False

Event OnConfigOpen()
	Bool corrected = CorrectDuplicateSlotSettings()
	RefreshSlotMenus()
	If corrected
		RefreshMenu()
	EndIf
EndEvent

Event OnSettingChange(String a_ID)
	If _refreshingSlotMenus
		Return
	EndIf

	If a_ID == EnableBondSetting || a_ID == NormalSlotSetting || a_ID == BondSlotSetting
		Bool corrected = CorrectDuplicateSlotSettings()
		RefreshSlotMenus()
		If corrected
			RefreshMenu()
		EndIf
	EndIf
EndEvent

Bool Function CorrectDuplicateSlotSettings()
	If !GetModSettingBool(EnableBondSetting)
		Return False
	EndIf

	String normalSlot = GetModSettingString(NormalSlotSetting)
	String bondSlot = GetModSettingString(BondSlotSetting)
	If !SlotValuesMatch(normalSlot, bondSlot)
		Return False
	EndIf

	String correctedBondSlot = DefaultBondSlot
	If SlotValuesMatch(correctedBondSlot, normalSlot)
		correctedBondSlot = GetFirstAvailableSlot(normalSlot)
	EndIf

	If correctedBondSlot == "" || SlotValuesMatch(correctedBondSlot, bondSlot)
		Return False
	EndIf

	SetModSettingString(BondSlotSetting, correctedBondSlot)
	Return True
EndFunction

Function RefreshSlotMenus()
	_refreshingSlotMenus = True

	If GetModSettingBool(EnableBondSetting)
		SetMenuOptions(NormalSlotSetting, GetSlotOptionsExcept(GetModSettingString(BondSlotSetting)), GetSlotShortNamesExcept(GetModSettingString(BondSlotSetting)))
		SetMenuOptions(BondSlotSetting, GetSlotOptionsExcept(GetModSettingString(NormalSlotSetting)), GetSlotShortNamesExcept(GetModSettingString(NormalSlotSetting)))
	Else
		SetMenuOptions(NormalSlotSetting, GetSlotOptions(), GetSlotShortNames())
		SetMenuOptions(BondSlotSetting, GetSlotOptions(), GetSlotShortNames())
	EndIf

	_refreshingSlotMenus = False
EndFunction

String Function GetFirstAvailableSlot(String a_blockedSlot)
	String[] options = GetSlotOptions()
	Int index = 0
	While index < options.Length
		If !SlotValuesMatch(options[index], a_blockedSlot)
			Return options[index]
		EndIf
		index += 1
	EndWhile

	Return ""
EndFunction

String[] Function GetSlotOptionsExcept(String a_excludedSlot)
	String[] options = GetSlotOptions()
	Int excludedIndex = FindSlotIndex(options, a_excludedSlot)
	If excludedIndex < 0
		Return options
	EndIf

	String[] filtered = New String[15]
	Int sourceIndex = 0
	Int targetIndex = 0
	While sourceIndex < options.Length
		If sourceIndex != excludedIndex
			filtered[targetIndex] = options[sourceIndex]
			targetIndex += 1
		EndIf
		sourceIndex += 1
	EndWhile

	Return filtered
EndFunction

String[] Function GetSlotShortNamesExcept(String a_excludedSlot)
	String[] options = GetSlotOptions()
	String[] shortNames = GetSlotShortNames()
	Int excludedIndex = FindSlotIndex(options, a_excludedSlot)
	If excludedIndex < 0
		Return shortNames
	EndIf

	String[] filtered = New String[15]
	Int sourceIndex = 0
	Int targetIndex = 0
	While sourceIndex < shortNames.Length
		If sourceIndex != excludedIndex
			filtered[targetIndex] = shortNames[sourceIndex]
			targetIndex += 1
		EndIf
		sourceIndex += 1
	EndWhile

	Return filtered
EndFunction

Int Function FindSlotIndex(String[] a_options, String a_slot)
	Int slotNumber = GetSlotNumber(a_slot)
	Int index = 0
	While index < a_options.Length
		If GetSlotNumber(a_options[index]) == slotNumber
			Return index
		EndIf
		index += 1
	EndWhile

	Return -1
EndFunction

Bool Function SlotValuesMatch(String a_left, String a_right)
	Return GetSlotNumber(a_left) == GetSlotNumber(a_right)
EndFunction

Int Function GetSlotNumber(String a_slot)
	If a_slot == "Face / Mouth (44)" || a_slot == "44"
		Return 44
	ElseIf a_slot == "Neck (45)" || a_slot == "45"
		Return 45
	ElseIf a_slot == "Chest Outer / Cloak (46)" || a_slot == "46"
		Return 46
	ElseIf a_slot == "Back (47)" || a_slot == "47"
		Return 47
	ElseIf a_slot == "Misc / FX (48)" || a_slot == "48"
		Return 48
	ElseIf a_slot == "Pelvis Primary / Skirt (49)" || a_slot == "49"
		Return 49
	ElseIf a_slot == "Pelvis Secondary / Undergarment (52)" || a_slot == "52"
		Return 52
	ElseIf a_slot == "Leg Right / Outer Leg (53)" || a_slot == "53"
		Return 53
	ElseIf a_slot == "Leg Left / Secondary Leg (54)" || a_slot == "54"
		Return 54
	ElseIf a_slot == "Face Alternate / Jewelry (55)" || a_slot == "55"
		Return 55
	ElseIf a_slot == "Chest Secondary / Torso (56)" || a_slot == "56"
		Return 56
	ElseIf a_slot == "Shoulder / Pauldron (57)" || a_slot == "57"
		Return 57
	ElseIf a_slot == "Arm Left / Secondary Arm (58)" || a_slot == "58"
		Return 58
	ElseIf a_slot == "Arm Right / Primary Arm (59)" || a_slot == "59"
		Return 59
	ElseIf a_slot == "Misc / Belt / FX (60)" || a_slot == "60"
		Return 60
	ElseIf a_slot == "FX01 (61)" || a_slot == "61"
		Return 61
	EndIf

	Return -1
EndFunction

String[] Function GetSlotOptions()
	String[] options = New String[16]
	options[0] = "Face / Mouth (44)"
	options[1] = "Neck (45)"
	options[2] = "Chest Outer / Cloak (46)"
	options[3] = "Back (47)"
	options[4] = "Misc / FX (48)"
	options[5] = "Pelvis Primary / Skirt (49)"
	options[6] = "Pelvis Secondary / Undergarment (52)"
	options[7] = "Leg Right / Outer Leg (53)"
	options[8] = "Leg Left / Secondary Leg (54)"
	options[9] = "Face Alternate / Jewelry (55)"
	options[10] = "Chest Secondary / Torso (56)"
	options[11] = "Shoulder / Pauldron (57)"
	options[12] = "Arm Left / Secondary Arm (58)"
	options[13] = "Arm Right / Primary Arm (59)"
	options[14] = "Misc / Belt / FX (60)"
	options[15] = "FX01 (61)"
	Return options
EndFunction

String[] Function GetSlotShortNames()
	String[] shortNames = New String[16]
	shortNames[0] = "44"
	shortNames[1] = "45"
	shortNames[2] = "46"
	shortNames[3] = "47"
	shortNames[4] = "48"
	shortNames[5] = "49"
	shortNames[6] = "52"
	shortNames[7] = "53"
	shortNames[8] = "54"
	shortNames[9] = "55"
	shortNames[10] = "56"
	shortNames[11] = "57"
	shortNames[12] = "58"
	shortNames[13] = "59"
	shortNames[14] = "60"
	shortNames[15] = "61"
	Return shortNames
EndFunction
