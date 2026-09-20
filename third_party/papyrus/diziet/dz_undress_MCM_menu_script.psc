Scriptname dz_undress_MCM_menu_script Extends SKI_ConfigBase
{;;;this script is for the mod Diziet's Player Home Bath Undressing's MCM menu in the game;;;}

;SCRIPT VERSION
Int Function GetVersion()
	Return 12																			;current script internal version number
EndFunction

;variables for use by trigger activator scripts
Bool	Property	disrobes			Auto	hidden									;externally accessible variable for player disrobing status
Bool	Property	third_person		Auto	hidden									;externally accessible variable for if player changes to third person
Bool	Property	ignoreCombat		Auto	hidden									;externally accessible variable for if player ignore combat status

;;;variables for version 1;;;
Bool	Property	toggle_allowplayer	Auto	hidden									;toggle for allowPlayerSetting
Bool	Property	toggle_thirdperson	Auto	hidden									;toggle for thirdPersonSetting
Bool	ignoreplayercombat																;toggle for combat setting

;;;properties and variables for version 2;;;
Actor	Property	PlayerRef			Auto											;allows faster reference to the player
Bool[]	Property	ignoremoreslots		Auto	hidden									;slot exemptions for the player
Bool[]	Property	ignoremorenpcslots	Auto	hidden									;slot exemptions for NPCs
Bool	Property	ignorehair			Auto	hidden									;externally accessible variable for not unequipping the player's hair slot
Bool	Property	ignorelonghair		Auto	hidden									;externally accessible variable for not unequpping the player's longhair slot
Bool	Property	ignorecirclet		Auto	hidden									;externally accessible variable for not unequipping the player's circlet slot
Bool	Property	ignorenpcCombat		Auto	hidden									;externally accessible variable for if NPCs ignore combat status
Bool	Property	ignorenpchair		Auto	hidden									;externally accessible variable for not unequipping NPCs hair slots
Bool	Property	ignorenpclonghair	Auto	hidden									;externally accessible variable for not unequipping NPCs long hair slots
Bool	Property	ignorenpccirclet	Auto	hidden									;externally accessible variable for not unequipping NPVs circlet slots
Bool	Property	player_hair_slots	Auto	hidden									;are we making hair slot exemptions for the player?
Bool	Property	npc_hair_slots		Auto	hidden									;are we making hair slot exemptions for NPCs
Bool	Property	all_slots			Auto	hidden									;are we making more complex slot exemptions (applies to both the player and NPCs)

Bool	toggle_hair																		;toggle for hair/slot31 setting
Bool	toggle_longhair																	;toggle for longhair/slot41 setting
Bool	toggle_circlet																	;toggle for circlet/slot42 setting
Bool	toggle_npc_hair																	;toggle for npcs' hair/slot31 setting
Bool	toggle_npc_longhair																;toggle for npcs' longhair/slot41 setting
Bool	toggle_npc_circlet																;toggle for npcs' circlet/slot42 setting
Bool	toggle_npc_combat																;toggle for npc combat setting
Bool	toggle_player_slot_choice														;toggle to enable extra player options
Bool	toggle_npc_slot_choice															;toggle to enable extra npc options
Bool	toggle_more_slot_options														;turn extra slot exemptions on/off
Bool	toggle_get_npc_slots															;toggle to examine NPC for equipped items
Bool[]	toggle_more_slots																;array to hold player equipped item slots
Bool[]	toggle_more_npc_slots															;array to hold NPC equipped item slots
String[]	_npc_text_slots																;array to hold NPC item names

;;;properties for version 5;;;
Bool		Property	slow_unequip	Auto	hidden									;use the slow unequip method (check for keep before unequip)
String[]	Property	keywords_list	Auto	hidden									;list of keywords for no stripping items
Bool	toggle_slow_unequip																;use slower unequip?
String teammate_name																	;will hold the NPC name
Actor teammate																			;get the reference for the NPC under the crosshair
Int		playerchoice																	;keep track of option flag to grey out extra options
Int		npcchoice																		;keep track of option flag to grey out extra options
Int		optionsenable																	;holds the greyed out status of some toggles
Int		slowequipchoice																	;keep track of option flag for slow equip function

;;;properties for version 8;;;
Bool Property	toggle_debug	Auto			hidden									;is debugging turned on in the menu?

;;;properties and variables for version 9 - cleaning spells;;;
Bool dz_clean_mods = False																;keep track of whether cleaning mods found so we can grey out the option if not
Bool Property	toggle_use_dab_clean_mod	Auto	hidden								;if cleaning mod present, toggle whether use it for player
Bool Property	toggle_npc_use_dab_clean_mod	Auto	hidden							;if cleaning mod present, toggle whether use it for npc
Bool Property	toggle_use_bis_clean_mod		Auto	hidden							;if cleaning mod present, toggle whether use it for player
Bool Property	toggle_npc_use_bis_clean_mod	Auto	hidden							;if cleaning mod present, toggle whether use it for npc
Spell	dz_dab_player_clean_spell														;dirt and blood player clean spell
Spell	dz_dab_NPC_clean_spell															;dirt and blood NPC clean spell
Int		DABCLEANMODSTATEOPTION															;keep track of option flag for dabusecleanmodstate
Int		DABNPCCLEANMODSTATEOPTION														;keep track of option flag for dabnpcusecleanmodstate
Int		BISCLEANMODSTATEOPTION															;keep track of option flag for usecleanmodstate
Int		BISNPCCLEANMODSTATEOPTION														;keep track of option flag for npcusecleanmodstate
String Property	cleanmod	Auto	hidden												;which cleaning mod to use
mzinBatheQuest	bis_cleanscript															;for Bathing In Skyrim script

;;;properties and variables for version 10;;;
Float player_clean_delay
Float npc_clean_delay
Int STATEPLAYERCLEANWAITOPTION
Int STATENPCCLEANWAITOPTION

;;;properties and variables for version 11;;;
Bool Property new_properties	Auto			hidden									;set to True by version updates indicates new properties and the need to restart the MCM quest;;;

;;;properties and variables for version 12;;;
Bool Property toggle_allowNPC	Auto			hidden

Function DEBUG_TRACE(Actor akActor, String sMsg)
	If toggle_debug
		If akActor
			debug.trace("DPHBU:"+self+": "+akActor.GetDisplayname()+": "+sMsg)
		Else
			debug.trace("DPHBU:"+self+": "+sMsg)
		EndIf
	EndIf
EndFunction

Event OnConfigInit()																	;when the menu is first run
	Pages = New String[4]
	Pages[0] = "$options"
	Pages[1] = "$even_more_options"
	Pages[2] = "$player_slots_used"
	Pages[3] = "$npc_slots_used"
	toggle_more_slots = New Bool[32]
	toggle_more_npc_slots = New Bool[32]
	ignoremoreslots = New Bool[32]

	ignoremorenpcslots = New Bool[32]
	_npc_text_slots = New String[32]

	;;;keywords list;;;
	keywords_list = New String[30]
	;keywords_list[0] = "dummy_keyword"
	;keywords_list[0] = "SexlabNoStrip"
	;keywords_list[1] = "SOS_Genitals"
	;JsonUtil.SetStringValue("../Diziets_Undressing/dz_undressing_config","dz_undressing_keywords", "test_string")
	;JsonUtil.Save("../Diziets_Undressing/dz_undressing_config")
	;List = New String[3]
	;List[1] = "dummy_keyword"
EndEvent

Event OnVersionUpdate(Int a_version)
	debug.trace("DPHBU - OnVersionUpdate event")
	If (a_version > CurrentVersion)
		debug.notification("Diziet's Undressing MCM menu updating to Version "+a_version)
	EndIf
	; a_version is the new version, CurrentVersion is the old version
	If (a_version >= 2 && CurrentVersion < 2)
		Debug.Trace(self + ": Updating Diziet's Undressing MCM script to version 2")
		;Debug.Notification("Diziet's Undressing MCM menu updating to Version 2")
		playerchoice = OPTION_FLAG_DISABLED
		npcchoice = OPTION_FLAG_DISABLED
		optionsenable = OPTION_FLAG_DISABLED
	EndIf

	;;;adds an extra slot (slot_61) to item exemption;;;
	If (a_version >= 3 && CurrentVersion < 3)
		Debug.Trace(self + ": Updating Diziet's Undressing MCM script to version 3")
		;Debug.Notification("Diziet's Undressing MCM menu updating to Version 3")
	EndIf

	;;;simply ups the version number to match the new version of the mod;;;
	If (a_version >= 4 && CurrentVersion < 4)
		Debug.Trace(self + ": Updating Diziet's Undressing MCM script to version 4")
		;Debug.Notification("Diziet's Undressing MCM menu updating to Version 4")
	EndIf

	If (a_version >= 5 && CurrentVersion < 5)
		Debug.Trace(self + ": Updating Diziet's Undressing MCM script to version 5")
		;Debug.Notification("Diziet's Undressing MCM menu updating to Version 5")
	EndIf

	If (a_version >= 6 && CurrentVersion < 6)
		Debug.Trace(self + ": Updating Diziet's Undressing MCM script to version 6")
		;Debug.Notification("Diziet's Undressing MCM menu updating to Version 6")
		slowequipchoice = OPTION_FLAG_DISABLED
	EndIf

	If (a_version >= 7 && CurrentVersion < 7)
		Debug.Trace(self + ": Updating Diziet's Undressing MCM script to version 7")
		;Debug.Notification("Diziet's Undressing MCM menu updating to Version 7")
		;/ Int i = 0
		While i < 32
			ignoremoreslots[i] = False
			ignoremorenpcslots[i] = False
			i += 1
		EndWhile /;
	EndIf

	If (a_version >= 8 && CurrentVersion < 8)
		Debug.Trace(self + ": Updating Diziet's Undressing MCM script to version 8")
		;Debug.Notification("Diziet's Undressing MCM menu updating to Version 8")
		toggle_debug = False															;set initial value of debugging toggle now it is introdced in this version
	EndIf

	If (a_version >= 9 && CurrentVersion < 9)
		Debug.Trace(self + ": Updating Diziet's Undressing MCM script to version 9")
		;Debug.Notification("Diziet's Undressing MCM menu updating to Version 9")
		;;;grey out option for using cleaning mod spells;;;
		DABCLEANMODSTATEOPTION = OPTION_FLAG_DISABLED
		DABNPCCLEANMODSTATEOPTION = OPTION_FLAG_DISABLED
		BISCLEANMODSTATEOPTION = OPTION_FLAG_DISABLED
		BISNPCCLEANMODSTATEOPTION = OPTION_FLAG_DISABLED
	EndIf

	If (a_version >= 10 && CurrentVersion < 10)
		Debug.Trace(self + ": Updating Diziet's Undressing MCM script to version 10")
		;Debug.Notification("Diziet's Undressing MCM menu updating to Version 10")
		player_clean_delay = 10															;set initial value
		npc_clean_delay = 10															;set initial value
		STATEPLAYERCLEANWAITOPTION = OPTION_FLAG_DISABLED
		STATENPCCLEANWAITOPTION = OPTION_FLAG_DISABLED
	EndIf

	If (a_version >= 11 && CurrentVersion < 11)
		Debug.Trace(self + ": Updating Diziet's Undressing MCM script to version 11")
		;Debug.Notification("Diziet's Undressing MCM menu updating to Version 11")
	EndIf

	If (a_version >= 12 && CurrentVersion < 12)
		Debug.Trace(self + ": Updating Diziet's Undressing MCM script to version 12")
		;Debug.Notification("Diziet's Undressing MCM menu updating to Version 12")
		toggle_allowNPC = True
	EndIf

	dz_maintenance()
	;;;dz_maintenance() function checks for cleaning mods present - currently "Dirt And Blood" and "Bathing in Skyrim";;;
	;;;also runs on playerloadgame via reference alias script;;;
EndEvent

Event OnConfigOpen()																	;runs whenever the MCM menu is opened in game
	teammate = Game.GetCurrentCrosshairRef() as Actor
	DEBUG_TRACE(NONE,"Event OnConfigOpen - actor (teammate) under crosshair is "+teammate)
	;String keylist = JsonUtil.GetStringValue("../Diziets_Undressing/dz_undressing_config","dz_undressing_keywords")
	;keywords_list = StringUtil.Split(keylist,",")

EndEvent

Event OnPageReset(String page)
	{Called when a new page is selected}
	If page == ""																		;is this the top page
		LoadCustomContent("diziet/bath_undressing.dds")									;show image
		Return
	else
		UnloadCustomContent()
	EndIf

	If page == "$options"
		SetTitletext("$options")
		SetCursorFillMode(TOP_TO_BOTTOM)
		;;;;;;;;;;;player options on left side of menu;;;;;;;;;;;;
		AddHeaderOption("$Player_Disrobing_Options")
		;AddEmptyOption()
		AddToggleOptionST("allowPlayerSetting","$Player_Undresses", toggle_allowplayer)
		AddToggleOptionST("thirdPersonSetting","$Force_Third_Person", toggle_thirdperson)
		AddToggleOptionST("combatSetting","$Disable_In_Combat_Check", ignoreplayercombat)
		AddEmptyOption()
		AddToggleOptionST("dabcleanmodstate","$player_dab_clean_mod_text",toggle_use_dab_clean_mod,DABCLEANMODSTATEOPTION)
		AddToggleOptionST("biscleanmodstate","$player_bis_clean_mod_text",toggle_use_bis_clean_mod,BISCLEANMODSTATEOPTION)
		AddSliderOptionST("state_player_clean_wait", "$choose_the_player_delay", player_clean_delay,"{0}",STATEPLAYERCLEANWAITOPTION)
		;AddEmptyOption()
		AddEmptyOption()
		AddHeaderOption("$player_slot_text")
		AddToggleOptionST("player_slot_choice","$player_slot_choice_text",toggle_player_slot_choice)
		AddEmptyOption()
		AddToggleOptionST("slot31option","$Do_not_unequip_the_hair_slot", toggle_hair,playerchoice)
		AddToggleOptionST("slot41option","$Do_not_unequip_the_longhair_slot", toggle_longhair,playerchoice)
		AddToggleOptionST("slot42option","$Do_not_unequip_the_circlet_slot", toggle_circlet,playerchoice)
		AddEmptyOption()
		AddHeaderOption("$slow_unequip_header_text")
		AddToggleOptionST("state_slow_unequip","$slow_unequip_text", toggle_slow_unequip)

		AddInputOptionST("state_keywords","$keyword_text","$input",slowequipchoice)
		AddMenuOptionST("state_list_keywords","$keyword_menu_text","$list",slowequipchoice)

		;;;;;;;;;NPC options on right side of menu;;;;;;;;;;;;;;
		SetCursorPosition(1)
		AddHeaderOption("$NPC_Disrobing_Options")
		AddToggleOptionST("state_allowNPCSetting","$NPCs_Undress", toggle_allowNPC)
		SetCursorPosition(7)
		AddToggleOptionST("npc_combat","$npc_disable_in_combat_check",toggle_npc_combat)
		AddEmptyOption()
		AddToggleOptionST("dabnpccleanmodstate","$npc_dab_clean_mod_text",toggle_npc_use_dab_clean_mod,DABNPCCLEANMODSTATEOPTION)
		AddToggleOptionST("bisnpccleanmodstate","$npc_bis_clean_mod_text",toggle_npc_use_bis_clean_mod,BISNPCCLEANMODSTATEOPTION)
		AddSliderOptionST("state_npc_clean_wait", "$choose_the_npc_delay", npc_clean_delay,"{0}",STATENPCCLEANWAITOPTION)
		;AddEmptyOption()
		AddEmptyOption()
		AddHeaderOption("$npc_slot_text")
		AddToggleOptionST("npc_slot_choice","$npc_slot_choice_text",toggle_npc_slot_choice)
		AddEmptyOption()
		AddToggleOptionST("npc_slot31option","$Do_not_unequip_the_npc_hair_slot", toggle_npc_hair,npcchoice)
		AddToggleOptionST("npc_slot41option","$Do_not_unequip_the_npc_longhair_slot", toggle_npc_longhair,npcchoice)
		AddToggleOptionST("npc_slot42option","$Do_not_unequip_the_npc_circlet_slot", toggle_npc_circlet,npcchoice)


		;;;add debug option;;;
		AddEmptyOption()
		AddEmptyOption()
		AddEmptyOption()
		AddHeaderOption("$other_options_text")
		AddToggleOptionST("state_debug","$debug_text", toggle_debug)

	EndIf

	If page == "$even_more_options"
		SetTitletext("$even_more_options")
		SetCursorFillMode(TOP_TO_BOTTOM)
		AddHeaderOption("$Show_more_slot_options")
		AddToggleOptionST("more_slot_options","$more_slot_options_text",toggle_more_slot_options)
		AddHeaderOption("$More_Player_Slots")

		Int i
		i = 30
		While i < 62
			AddToggleOptionST("more_slots"+i,"$Slot_"+i,toggle_more_slots[i - 30],optionsenable)
			i = i + 1
		EndWhile

		SetCursorPosition(5)
		AddHeaderOption("$More_NPC_slots")
		Int j
		j = 30
		While j < 62
			AddToggleOptionST("more_npc_slots"+j,"$Slot_"+j,toggle_more_npc_slots[j - 30],optionsenable)
			j = j + 1
		EndWhile

	EndIf

	If page == "$player_slots_used"
	;;;this code will produce two errors for each PlayerRef.GetEquippedArmorInSlot(i).GetName()that fails;;;
	;;;see Function dz_fill_npc_slots_used() lower down;;;
		SetTitletext("player_slots_used")
		;;;entries in the left column;;;
		SetCursorFillMode(TOP_TO_BOTTOM)
		String[] _text_slots
		_text_slots = New String[32]
		Int i
		i = 30
		While i < 46
			If PlayerRef.GetEquippedArmorInSlot(i).GetName()
			_text_slots[i - 30] = PlayerRef.GetEquippedArmorInSlot(i).GetName()
			Else
			_text_slots[i - 30] = "$empty"
			EndIf
			AddHeaderOption("$Slot_"+i)
			AddTextOptionST("$slot"+i,"",_text_slots[i - 30])
			i = i + 1
		EndWhile

		;;;entries in the right column;;;
		SetCursorPosition(1)
		SetCursorFillMode(TOP_TO_BOTTOM)
		i = 46
		While i < 62
			If PlayerRef.GetEquippedArmorInSlot(i).GetName()
			_text_slots[i - 30] = PlayerRef.GetEquippedArmorInSlot(i).GetName()
			Else
			_text_slots[i - 30] = "$empty"
			EndIf
			AddHeaderOption("$Slot_"+i)
			AddTextOptionST("$slot"+i,"",_text_slots[i - 30])
			i = i + 1
		EndWhile
	EndIf

	If page == "$npc_slots_used"
		SetTitletext("$npc_slots_used")
		SetCursorFillMode(TOP_TO_BOTTOM)
		AddHeaderOption("$Examine_an_NPC's_slots")
		AddToggleOptionST("state_get_npc_slots","$get_npc_slots_text",toggle_get_npc_slots)
		AddTextOptionST("state_text_get_npc_slots","$npc_name",teammate_name)
		Int i
		i = 30
		While i < 46
			AddHeaderOption("$Slot_"+i)
			AddTextOptionST("$slot"+i,"",_npc_text_slots[i - 30])
			i = i + 1
		EndWhile

		SetCursorPosition(7)
		SetCursorFillMode(TOP_TO_BOTTOM)
		i = 46
		While i < 62
			AddHeaderOption("$Slot_"+i)
			AddTextOptionST("$slot"+i,"",_npc_text_slots[i - 30])
			i = i + 1
		EndWhile
	EndIf
EndEvent

;;;states for cleaning mod options;;;
;;;state for cleaning delays;;;
State state_player_clean_wait
	Event OnSliderOpenST()
		SetSliderDialogStartValue(player_clean_delay)
		SetSliderDialogDefaultValue(10)
		SetSliderDialogRange(1,30)
		SetSliderDialogInterval(1)
	EndEvent

	Event OnSliderAcceptST(Float a_value)
		player_clean_delay = a_value
		SetSliderOptionValueST(player_clean_delay)
		ForcePageReset()
	EndEvent

	Event OnDefaultST()
		player_clean_delay = 10
		SetSliderOptionValueST(player_clean_delay)
	EndEvent

	Event OnHighlightST()
		SetInfoText("$player_clean_delay_info")
	EndEvent
EndState

State state_npc_clean_wait
	Event OnSliderOpenST()
		SetSliderDialogStartValue(npc_clean_delay)
		SetSliderDialogDefaultValue(10)
		SetSliderDialogRange(1,30)
		SetSliderDialogInterval(1)
	EndEvent

	Event OnSliderAcceptST(Float a_value)
		npc_clean_delay = a_value
		SetSliderOptionValueST(npc_clean_delay)
		ForcePageReset()
	EndEvent

	Event OnDefaultST()
		npc_clean_delay = 10
		SetSliderOptionValueST(npc_clean_delay)
	EndEvent

	Event OnHighlightST()
		SetInfoText("$npc_clean_delay_info")
	EndEvent
EndState
;;;end of states for cleaning delays;;;

;;;states for Dirt and Blood mod;;;
State dabcleanmodstate
	Event OnSelectST()
		toggle_use_dab_clean_mod = !toggle_use_dab_clean_mod
		SetToggleOptionValueST(toggle_use_dab_clean_mod)
		If toggle_use_dab_clean_mod == True
			toggle_use_bis_clean_mod = False
			;SetToggleOptionValueST(toggle_use_bis_clean_mod)
		EndIf
	EndEvent

	Event OnDefaultST()
		toggle_use_dab_clean_mod = False
		SetToggleOptionValueST(toggle_use_dab_clean_mod)
	EndEvent
	Event OnHighlightST()
		SetInfoText("$use_dab_clean_mod_info_text")
	EndEvent
EndState

State dabnpccleanmodstate
	Event OnSelectST()
		toggle_npc_use_dab_clean_mod = !toggle_npc_use_dab_clean_mod
		SetToggleOptionValueST(toggle_npc_use_dab_clean_mod)
		If toggle_npc_use_dab_clean_mod == True
			toggle_npc_use_bis_clean_mod = False
			;SetToggleOptionValueST(toggle_npc_use_bis_clean_mod)
		EndIf
	EndEvent

	Event OnDefaultST()
		toggle_npc_use_dab_clean_mod = False
		SetToggleOptionValueST(toggle_npc_use_dab_clean_mod)
	EndEvent
	Event OnHighlightST()
		SetInfoText("$npc_use_dab_clean_mod_info_text")
	EndEvent
EndState
;;;end of states for Dirt and Blood mod;;;

;;;states for Bathing in Skyrim mod;;;
State biscleanmodstate
	Event OnSelectST()
		toggle_use_bis_clean_mod = !toggle_use_bis_clean_mod
		SetToggleOptionValueST(toggle_use_bis_clean_mod)
		If toggle_use_bis_clean_mod == True
			toggle_use_dab_clean_mod = False
			;SetToggleOptionValueST(toggle_use_dab_clean_mod)
		EndIf
	EndEvent

	Event OnDefaultST()
		toggle_use_bis_clean_mod = False
		SetToggleOptionValueST(toggle_use_bis_clean_mod)
	EndEvent
	Event OnHighlightST()
		SetInfoText("$use_bis_clean_mod_info_text")
	EndEvent
EndState

State bisnpccleanmodstate
	Event OnSelectST()
		toggle_npc_use_bis_clean_mod = !toggle_npc_use_bis_clean_mod
		SetToggleOptionValueST(toggle_npc_use_bis_clean_mod)
		If toggle_npc_use_bis_clean_mod == True
			toggle_npc_use_dab_clean_mod = False
			;SetToggleOptionValueST(toggle_npc_use_dab_clean_mod)
		EndIf
	EndEvent

	Event OnDefaultST()
		toggle_npc_use_bis_clean_mod = False
		SetToggleOptionValueST(toggle_npc_use_bis_clean_mod)
	EndEvent
	Event OnHighlightST()
		SetInfoText("$npc_use_bis_clean_mod_info_text")
	EndEvent
EndState
;;;end of states for Bathing in Skyrim mod;;;
;;;end of states for cleaning mod options;;;

State state_get_npc_slots
	Event OnSelectST()
		toggle_get_npc_slots = !toggle_get_npc_slots
		SetToggleOptionValueST(toggle_get_npc_slots)
		If toggle_get_npc_slots == True
			dz_fill_npc_slots_used()
		ElseIf toggle_get_npc_slots == False
			dz_empty_npc_slots_used()
		EndIf
	EndEvent
EndState

State	allowPlayerSetting																;toggle settings for allowPlayerSetting
	Event OnSelectST()
		toggle_allowplayer = !toggle_allowplayer										;swap the current state of the toggle
	SetToggleOptionValueST(toggle_allowplayer)											;show the new state of the toggle
		If (toggle_allowplayer == True)													;if new toggle is on
		Disrobes = True																	;then player disrobing will happen
		ElseIf (toggle_allowplayer == False)											;if new toggle is off
		Disrobes = False																;then player disrobing will not happen
		EndIf
	EndEvent

	Event OnDefaultST()																	;what is the default setting for this toggle
		toggle_allowplayer = True														;default is on
		Disrobes = True																	;default is player undressing
		SetToggleOptionValueST(toggle_allowplayer)										;set the toggle to the default (on)
	EndEvent
	Event OnHighlightST()																;text shown on mouse hover over this toggle
		SetInfoText("$highlight_allow_player_setting_text")
	EndEvent
EndState

State	thirdPersonSetting																;toggle settings for thirdPersonSetting
	Event OnSelectST()
		toggle_thirdperson = !toggle_thirdperson										;swap the current state of the toggle
		SetToggleOptionValueST(toggle_thirdperson)										;show the new state of the toggle
		If (toggle_thirdperson == True)													;if new toggle is on
		third_person = True																;player view becomes third person
		ElseIf (toggle_thirdperson == False)											;if new toggle is off
		third_person = False															; player view stays as is
		EndIf
	EndEvent

	Event OnDefaultST()																	;what is the default setting for this toggle
		toggle_thirdperson = True														;default is on
		third_person = True																;default is change to third person
		SetToggleOptionValueST(toggle_thirdperson)										;set the toggle to the default (on)
	EndEvent
	Event OnHighlightST()																;text shown on mouse hover over this toggle
		SetInfoText("$highlight_third_person_setting")
	EndEvent
EndState

State	combatSetting																	;toggle settings for combatSetting
	Event OnSelectST()
		ignoreplayercombat = !ignoreplayercombat										;swap the current state of the toggle
		SetToggleOptionValueST(ignoreplayercombat)										;show the new state of the toggle
		If (ignoreplayercombat == True)													;if new toggle is on
		ignoreCombat = True																;ignore combat status
		ElseIf (ignoreplayercombat == False)											;if new toggle is off
		ignoreCombat = False															;do not ignore combat status
		EndIf
	EndEvent

	Event OnDefaultST()																	;what is the default setting for this toggle
		ignoreplayercombat = True														;default is on
		ignoreCombat = True																;default is ignore combat status
		SetToggleOptionValueST(ignoreplayercombat)										;set the toggle to the default (on)
	EndEvent

	Event OnHighlightST()																;text shown on mouse hover over this toggle
		SetInfoText("$highlight_combat_info_text")										;show the text replaced for $highlight_combat_info_text from the data/interface/translations/*LANG.txt
	EndEvent
EndState

;;;states for alternative slow unequipping method;;;
State state_slow_unequip																;for an alternative unequipping method to avoid issues with some mods
	Event OnSelectST()
		toggle_slow_unequip = !toggle_slow_unequip
		SetToggleOptionValueST(toggle_slow_unequip)
		If toggle_slow_unequip == True
			slow_unequip = True
			slowequipchoice = OPTION_FLAG_NONE
			SetOptionFlagsST(OPTION_FLAG_NONE,False,"state_keywords")
			SetOptionFlagsST(OPTION_FLAG_NONE,False,"state_list_keywords")
		ElseIf toggle_slow_unequip == False
			slow_unequip = False
			slowequipchoice = OPTION_FLAG_DISABLED
			SetOptionFlagsST(OPTION_FLAG_DISABLED,False,"state_keywords")
			SetOptionFlagsST(OPTION_FLAG_DISABLED,False,"state_list_keywords")
		EndIf
	EndEvent

	Event OnDefaultST()
		toggle_slow_unequip = False
		slow_unequip = False
		SetToggleOptionValueST(toggle_slow_unequip)
	EndEvent

	Event OnHighlightST()
		SetInfoText("$slow_unequip_info_text")
	EndEvent
EndState

State state_keywords
	Event OnInputOpenST()

	EndEvent
	Event OnInputAcceptST(String new_keyword)
		;String[] List = new String[3]
		;List[0] = "dummy_keyword"
		;PO3_SKSEFunctions.AddStringToArray(new_keyword,keywords_list)
		ArrayAddString(keywords_list,new_keyword)
		;ShowMessage("keywords are "+keywords_list,False)
	EndEvent

	Event OnHighlightST()
		SetInfoText("$keywords_info_text")
	EndEvent
EndState

State state_list_keywords
	Event OnMenuOpenST()
		SetMenuDialogOptions(keywords_list)
		;SetMenuDialogStartIndex(1)
	EndEvent

	Event OnMenuAcceptST(Int index)
		If keywords_list[index] != ""
			If ShowMessage("$remove_the_keyword{"+keywords_list[index]+"}")
			ArrayRemoveString(keywords_list,keywords_list[index],True)
			Else
			EndIf
			;keywords_list[index] = NONE
			;SetMenuOptionValueST(keywords_list[index])
		EndIf
	EndEvent
EndState
;;;end of states for alternative slow unequipping method;;;

State	slot31option
	Event OnSelectST()
		toggle_hair = !toggle_hair														;swap the current state of the toggle
		SetToggleOptionValueST(toggle_hair)												;show the new state of the toggle
		If toggle_hair == True															;if new toggle is on
		ignorehair = True																;do not unequip hair slot
		ElseIf toggle_hair == False														;if new toggle is off
		ignorehair = False																;unequip hair slot
		EndIf
		dz_booleans_check()
	EndEvent

	Event OnDefaultST()																	;what is the default setting for this toggle
		toggle_hair = False																;default is off
		ignorehair = False																;default is unequip hair slot
		SetToggleOptionValueST(toggle_hair)												;set the toggle to the default (off)
	EndEvent

	Event OnHighlightST()
		SetInfoText("$highlight_hair_slot_text")										;show the text replaced for $highlight_hair_slot_text from the data/interface/translations/*LANG.txt
	EndEvent
EndState

State	slot41option
	Event OnSelectST()
		toggle_longhair = !toggle_longhair												;swap the current state of the toggle
		SetToggleOptionValueST(toggle_longhair)											;show the new state of the toggle
		If toggle_longhair == True														;if new toggle is on
		ignorelonghair = True															;do not unequip thelong hair slot
		ElseIf toggle_longhair == False													;if new toggle is off
		ignorelonghair = False															;unequip the long hair slot
		EndIf
		dz_booleans_check()
	EndEvent

	Event OnDefaultST()																	;what is the default setting for this toggle
		toggle_longhair = False															;default is off
		ignorelonghair = False															;default is unequip the long hair slot
		SetToggleOptionValueST(toggle_longhair)											;set the toggle to the default (off)
	EndEvent

	Event OnHighlightST()
		SetInfoText("$highlight_long_hair_slot_text")									;show the text replaced for $highlight_long_hair_slot_text from the data/interface/translations/*LANG.txt
	EndEvent
EndState

State	slot42option
	Event OnSelectST()
		toggle_circlet = !toggle_circlet												;swap the current state of the toggle
		SetToggleOptionValueST(toggle_circlet)											;show the new state of the toggle
		If toggle_circlet == True														;if new toggle is on
		ignorecirclet = True															;do not unequip the circlet slot
		ElseIf toggle_circlet == False													;if new toggle is off
		ignorecirclet = False															;unequip the circlet slot
		EndIf
		dz_booleans_check()
	EndEvent

	Event OnDefaultST()																	;what is the default setting for this toggle
		toggle_circlet = False															;default is off
		ignorecirclet = False															;default is unequip the circlet slot
		SetToggleOptionValueST(toggle_circlet)											;set the toggle to the default (off)
	EndEvent

	Event OnHighlightST()
		SetInfoText("$highlight_circlet_slot_text")										;show the text replaced for $highlight_circlet_slot_text from the data/interface/translations/*LANG.txt
	EndEvent
EndState

State	state_allowNPCSetting
	Event OnSelectST()
		toggle_allowNPC = !toggle_allowNPC												;swap the current state of the toggle
	SetToggleOptionValueST(toggle_allowNPC)												;show the new state of the toggle
	EndEvent

	Event OnDefaultST()																	;what is the default setting for this toggle
		toggle_allowNPC = True															;default is on
		SetToggleOptionValueST(toggle_allowNPC)											;set the toggle to the default (on)
	EndEvent
	Event OnHighlightST()																;text shown on mouse hover over this toggle
		SetInfoText("$highlight_allow_NPCs_setting_text")
	EndEvent
EndState

State	npc_combat																		;toggle settings for npc_combat
	Event OnSelectST()
		toggle_npc_combat = !toggle_npc_combat											;swap the current state of the toggle
		SetToggleOptionValueST(toggle_npc_combat)										;show the new state of the toggle
		If (toggle_npc_combat == True)													;if new toggle is on
		ignorenpcCombat = True															;ignore npc combat status
		ElseIf (toggle_npc_combat == False)												;if new toggle is off
		ignorenpcCombat = False															;do not ignore npc combat status
		EndIf
	EndEvent

	Event OnDefaultST()																	;what is the default setting for this toggle
		toggle_npc_combat = True														;default is on
		ignorenpcCombat = True															;default is ignore npc combat status
		SetToggleOptionValueST(toggle_npc_combat)										;set the toggle to the default (on)
	EndEvent

	Event OnHighlightST()																;text shown on mouse hover over this toggle
		SetInfoText("$highlight_npc_combat_info_text")									;show the text replaced for $highlight_npc_combat_info_text from the data/interface/translations/*LANG.txt
	EndEvent
EndState

State	npc_slot31option
	Event OnSelectST()
		toggle_npc_hair = !toggle_npc_hair												;swap the current state of the toggle
		SetToggleOptionValueST(toggle_npc_hair)											;show the new state of the toggle
		If toggle_npc_hair == True														;if new toggle is on
		ignorenpchair = True															;do not unequip NPCs hair slots
		ElseIf toggle_npc_hair == False													;if new toggle is off
		ignorenpchair = False															;unequip NPCs hair slots
		EndIf
		dz_booleans_check()
	EndEvent

	Event OnDefaultST()																	;what is the default setting for this toggle
		toggle_hair = False																;default is off
		ignorenpchair = False															;default is unequip NPCs hair slots
		SetToggleOptionValueST(toggle_hair)												;set the toggle to the default (off)
		SetOptionFlagsST(playerchoice)
	EndEvent

	Event OnHighlightST()
		SetInfoText("$highlight_hair_slot_text")										;show the text replaced for $highlight_hair_slot_text from the data/interface/translations/*LANG.txt
	EndEvent
EndState

State	npc_slot41option
	Event OnSelectST()
		toggle_npc_longhair = !toggle_npc_longhair										;swap the current state of the toggle
		SetToggleOptionValueST(toggle_npc_longhair)										;show the new state of the toggle
		If toggle_npc_longhair == True													;if new toggle is on
		ignorenpclonghair = True														;do not unequip the NPCs long hair slot
		ElseIf toggle_npc_longhair == False												;if new toggle is off
		ignorenpclonghair = False														;unequip NPCs long hair slots
		EndIf
		dz_booleans_check()
	EndEvent

	Event OnDefaultST()																	;what is the default setting for this toggle
		toggle_npc_longhair = False														;default is off
		ignorenpclonghair = False														;default is unequip the NPCs long hair slots
		SetToggleOptionValueST(toggle_npc_longhair)										;set the toggle to the default (off)
	EndEvent

	Event OnHighlightST()
		SetInfoText("$highlight_long_hair_slot_text")									;show the text replaced for $highlight_long_hair_slot_text from the data/interface/translations/*LANG.txt
	EndEvent
EndState

State	npc_slot42option
	Event OnSelectST()
		toggle_npc_circlet = !toggle_npc_circlet										;swap the current state of the toggle
		SetToggleOptionValueST(toggle_npc_circlet)										;show the new state of the toggle
		If toggle_npc_circlet == True													;if new toggle is on
		ignorenpccirclet = True															;do not unequip NPCs circlet slots
		ElseIf toggle_npc_circlet == False												;if new toggle is off
		ignorenpccirclet = False														;unequip NPCs circlet slots
		EndIf
		dz_booleans_check()
	EndEvent

	Event OnDefaultST()																	;what is the default setting for this toggle
		toggle_npc_circlet = False														;default is off
		ignorenpccirclet = False														;default is unequip NPCs circlet slots
		SetToggleOptionValueST(toggle_npc_circlet)										;set the toggle to the default (off)
	EndEvent

	Event OnHighlightST()
		SetInfoText("$highlight_circlet_slot_text")										;show the text replaced for $highlight_circlet_slot_text from the data/interface/translations/*LANG.txt
	EndEvent
EndState

State	player_slot_choice
	Event OnSelectST()
		toggle_player_slot_choice = !toggle_player_slot_choice							;swap the current state of the toggle
		SetToggleOptionValueST(toggle_player_slot_choice)								;show the new state of the toggle
		If toggle_player_slot_choice == True  && ShowMessage("$individual_slot_choice_text")	;if toggle is on
			dztoggle_player_slots_on()
			player_hair_slots = True
			toggle_more_slot_options = False
		ElseIf toggle_player_slot_choice == False										;if toggle is off
			dztoggle_player_slots_off()
			player_hair_slots = False
		Else
			toggle_player_slot_choice = False
			SetToggleOptionValueST(toggle_player_slot_choice)
			player_hair_slots = False
		EndIf
	EndEvent

	Event OnDefaultST()																	;what is the default setting for this toggle
		toggle_player_slot_choice = False												;default is off
		playerchoice = OPTION_FLAG_DISABLED
		player_hair_slots = False
		SetOptionFlagsST(playerchoice,False,"slot31option")								;default is greyed out
		SetOptionFlagsST(playerchoice,False,"slot41option")								;default is greyed out
		SetOptionFlagsST(playerchoice,False,"slot42option")								;default is greyed out
		SetToggleOptionValueST(toggle_player_slot_choice)								;set the toggle to the default (off)
	EndEvent

	Event OnHighlightST()
		SetInfoText("$highlight_player_slot_text")										;show the text replaced for $highlight_player_slot_text from the data/interface/translations/*LANG.txt
	EndEvent
EndState

State	npc_slot_choice
	Event OnSelectST()
		toggle_npc_slot_choice = !toggle_npc_slot_choice								;swap the current state of the toggle
		SetToggleOptionValueST(toggle_npc_slot_choice)									;show the new state of the toggle
		If toggle_npc_slot_choice == True  && ShowMessage("$individual_npc_slot_choice_text")	;if toggle is on
			dztoggle_npc_slots_on()
			npc_hair_slots = True
			toggle_more_slot_options = False
		ElseIf toggle_npc_slot_choice == False											;if toggle is off
			dztoggle_npc_slots_off()
			npc_hair_slots = False
		Else
			toggle_npc_slot_choice = False
			npc_hair_slots = False
			SetToggleOptionValueST(toggle_npc_slot_choice)
		EndIf
	EndEvent

	Event OnDefaultST()																	;what is the default setting for this toggle
		toggle_npc_slot_choice = False													;default is off
		npcchoice = OPTION_FLAG_DISABLED
		npc_hair_slots = False
		SetOptionFlagsST(npcchoice,False,"npc_slot31option")							;default is greyed out
		SetOptionFlagsST(npcchoice,False,"npc_slot41option")							;default is greyed out
		SetOptionFlagsST(npcchoice,False,"npc_slot42option")							;default is greyed out
		SetToggleOptionValueST(toggle_npc_slot_choice)									;set the toggle to the default (off)
	EndEvent

	Event OnHighlightST()
		SetInfoText("$highlight_npc_slot_text")											;show the text replaced for $highlight_npc_slot_text from the data/interface/translations/*LANG.txt
	EndEvent
EndState

State	more_slot_options
	Event OnSelectST()
		toggle_more_slot_options = !toggle_more_slot_options
		SetToggleOptionValueST(toggle_more_slot_options)
		If	toggle_more_slot_options == True && ShowMessage("$toggle_more_options_text")
			dz_more_slot_options_on()
			all_slots = True
		ElseIf	toggle_more_slot_options == False
			dz_more_slot_options_off()
			all_slots = False
		Else
			toggle_more_slot_options = False
			SetToggleOptionValueST(toggle_more_slot_options)
			all_slots = False
		EndIf
	EndEvent

	Event OnDefaultST()
		toggle_more_slot_options = False
		optionsenable = OPTION_FLAG_DISABLED
		Int i
		i = 30
		While i < 62
		String j = i As String
			SetOptionFlagsST(OPTION_FLAG_DISABLED,False,"more_slots"+j)
			i = i + 1
		EndWhile
		SetToggleOptionValueST(toggle_more_slot_options)
	EndEvent

	Event OnHighlightST()
		SetInfoText("$highlight_more_options_text")
	EndEvent
EndState

State	more_slots30
	Event OnSelectST()
		toggle_more_slots[0] = !toggle_more_slots[0]									;swap the current state of the toggle
		SetToggleOptionValueST(toggle_more_slots[0])
		If toggle_more_slots[0] == True													;if new toggle is on
		ignoremoreslots[0] = True														;do not unequip slot 0
		ElseIf toggle_more_slots[0] == False											;if new toggle is off
		ignoremoreslots[0] = False														;unequip slot 0
		EndIf
	EndEvent

	Event OnDefaultST()
		toggle_more_slots[0] = False													;default is off
		ignoremoreslots[0] = False														;default is unequip slot 0
		SetToggleOptionValueST(toggle_more_slots[0])									;set toggle to default (off)
	EndEvent

	Event OnHighlightST()
		SetInfoText("")																	;no highlight text
	EndEvent
EndState

State	more_slots31
	Event OnSelectST()
		toggle_more_slots[1] = !toggle_more_slots[1]									;swap the current state of the toggle
		SetToggleOptionValueST(toggle_more_slots[1])
		If toggle_more_slots[1] == True													;if new toggle is on
		ignoremoreslots[1] = True														;do not unequip slot 1
		ElseIf toggle_more_slots[1] == False											;if new toggle is off
		ignoremoreslots[1] = False														;unequip slot 1
		EndIf
	EndEvent

	Event OnDefaultST()
		toggle_more_slots[1] = False													;default is off
		ignoremoreslots[1] = False														;default is unequip slot 1
		SetToggleOptionValueST(toggle_more_slots[1])									;set toggle to default (off)
	EndEvent

	Event OnHighlightST()
		SetInfoText("")																	;no highlight text
	EndEvent
EndState

State	more_slots32
	Event OnSelectST()
		toggle_more_slots[2] = !toggle_more_slots[2]									;swap the current state of the toggle
		SetToggleOptionValueST(toggle_more_slots[2])
		If toggle_more_slots[2] == True													;if new toggle is on
		ignoremoreslots[2] = True														;do not unequip slot 2
		ElseIf toggle_more_slots[2] == False											;if new toggle is off
		ignoremoreslots[2] = False														;unequip slot 2
		EndIf
	EndEvent

	Event OnDefaultST()
		toggle_more_slots[2] = False													;default is off
		ignoremoreslots[2] = False														;default is unequip slot 2
		SetToggleOptionValueST(toggle_more_slots[2])									;set toggle to default (off)
	EndEvent

	Event OnHighlightST()
		SetInfoText("")																	;no highlight text
	EndEvent
EndState

State	more_slots33
	Event OnSelectST()
		toggle_more_slots[3] = !toggle_more_slots[3]									;swap the current state of the toggle
		SetToggleOptionValueST(toggle_more_slots[3])
		If toggle_more_slots[3] == True													;if new toggle is on
		ignoremoreslots[3] = True														;do not unequip slot 3
		ElseIf toggle_more_slots[3] == False											;if new toggle is off
		ignoremoreslots[3] = False														;unequip slot 3
		EndIf
	EndEvent

	Event OnDefaultST()
		toggle_more_slots[3] = False													;default is off
		ignoremoreslots[3] = False														;default is unequip slot 3
		SetToggleOptionValueST(toggle_more_slots[3])									;set toggle to default (off)
	EndEvent

	Event OnHighlightST()
		SetInfoText("")																	;no highlight text
	EndEvent
EndState

State	more_slots34
	Event OnSelectST()
		toggle_more_slots[4] = !toggle_more_slots[4]									;swap the current state of the toggle
		SetToggleOptionValueST(toggle_more_slots[4])
		If toggle_more_slots[4] == True													;if new toggle is on
		ignoremoreslots[4] = True														;do not unequip slot 4
		ElseIf toggle_more_slots[4] == False											;if new toggle is off
		ignoremoreslots[4] = False														;unequip slot 4
		EndIf
	EndEvent

	Event OnDefaultST()
		toggle_more_slots[4] = False													;default is off
		ignoremoreslots[4] = False														;default is unequip slot 4
		SetToggleOptionValueST(toggle_more_slots[4])									;set toggle to default (off)
	EndEvent

	Event OnHighlightST()
		SetInfoText("")																	;no highlight text
	EndEvent
EndState

State	more_slots35
	Event OnSelectST()
		toggle_more_slots[5] = !toggle_more_slots[5]									;swap the current state of the toggle
		SetToggleOptionValueST(toggle_more_slots[5])
		If toggle_more_slots[5] == True													;if new toggle is on
		ignoremoreslots[5] = True														;do not unequip slot 5
		ElseIf toggle_more_slots[5] == False											;if new toggle is off
		ignoremoreslots[5] = False														;unequip slot 5
		EndIf
	EndEvent

	Event OnDefaultST()
		toggle_more_slots[5] = False													;default is off
		ignoremoreslots[5] = False														;default is unequip slot 5
		SetToggleOptionValueST(toggle_more_slots[5])									;set toggle to default (off)
	EndEvent

	Event OnHighlightST()
		SetInfoText("")																	;no highlight text
	EndEvent
EndState

State	more_slots36
	Event OnSelectST()
		toggle_more_slots[6] = !toggle_more_slots[6]									;swap the current state of the toggle
		SetToggleOptionValueST(toggle_more_slots[6])
		If toggle_more_slots[6] == True													;if new toggle is on
		ignoremoreslots[6] = True														;do not unequip slot 6
		ElseIf toggle_more_slots[6] == False											;if new toggle is off
		ignoremoreslots[6] = False														;unequip slot 6
		EndIf
	EndEvent

	Event OnDefaultST()
		toggle_more_slots[6] = False													;default is off
		ignoremoreslots[6] = False														;default is unequip slot 6
		SetToggleOptionValueST(toggle_more_slots[6])									;set toggle to default (off)
	EndEvent

	Event OnHighlightST()
		SetInfoText("")																	;no highlight text
	EndEvent
EndState

State	more_slots37
	Event OnSelectST()
		toggle_more_slots[7] = !toggle_more_slots[7]									;swap the current state of the toggle
		SetToggleOptionValueST(toggle_more_slots[7])
		If toggle_more_slots[7] == True													;if new toggle is on
		ignoremoreslots[7] = True														;do not unequip slot 7
		ElseIf toggle_more_slots[7] == False											;if new toggle is off
		ignoremoreslots[7] = False														;unequip slot 7
		EndIf
	EndEvent

	Event OnDefaultST()
		toggle_more_slots[7] = False													;default is off
		ignoremoreslots[7] = False														;default is unequip slot 7
		SetToggleOptionValueST(toggle_more_slots[7])									;set toggle to default (off)
	EndEvent

	Event OnHighlightST()
		SetInfoText("")																	;no highlight text
	EndEvent
EndState

State	more_slots38
	Event OnSelectST()
		toggle_more_slots[8] = !toggle_more_slots[8]									;swap the current state of the toggle
		SetToggleOptionValueST(toggle_more_slots[8])
		If toggle_more_slots[8] == True													;if new toggle is on
		ignoremoreslots[8] = True														;do not unequip slot 8
		ElseIf toggle_more_slots[8] == False											;if new toggle is off
		ignoremoreslots[8] = False														;unequip slot 8
		EndIf
	EndEvent

	Event OnDefaultST()
		toggle_more_slots[8] = False													;default is off
		ignoremoreslots[8] = False														;default is unequip slot 8
		SetToggleOptionValueST(toggle_more_slots[8])									;set toggle to default (off)
	EndEvent

	Event OnHighlightST()
		SetInfoText("")																	;no highlight text
	EndEvent
EndState

State	more_slots39
	Event OnSelectST()
		toggle_more_slots[9] = !toggle_more_slots[9]									;swap the current state of the toggle
		SetToggleOptionValueST(toggle_more_slots[9])
		If toggle_more_slots[9] == True													;if new toggle is on
		ignoremoreslots[9] = True														;do not unequip slot 9
		ElseIf toggle_more_slots[9] == False											;if new toggle is off
		ignoremoreslots[9] = False														;unequip slot 9
		EndIf
	EndEvent

	Event OnDefaultST()
		toggle_more_slots[9] = False													;default is off
		ignoremoreslots[9] = False														;default is unequip slot 9
		SetToggleOptionValueST(toggle_more_slots[9])									;set toggle to default (off)
	EndEvent

	Event OnHighlightST()
		SetInfoText("")																	;no highlight text
	EndEvent
EndState

State	more_slots40
	Event OnSelectST()
		toggle_more_slots[10] = !toggle_more_slots[10]									;swap the current state of the toggle
		SetToggleOptionValueST(toggle_more_slots[10])
		If toggle_more_slots[10] == True												;if new toggle is on
		ignoremoreslots[10] = True														;do not unequip slot 10
		ElseIf toggle_more_slots[10] == False											;if new toggle is off
		ignoremoreslots[10] = False														;unequip slot 10
		EndIf
	EndEvent

	Event OnDefaultST()
		toggle_more_slots[10] = False													;default is off
		ignoremoreslots[10] = False														;default is unequip slot 10
		SetToggleOptionValueST(toggle_more_slots[10])									;set toggle to default (off)
	EndEvent

	Event OnHighlightST()
		SetInfoText("")																	;no highlight text
	EndEvent
EndState

State	more_slots41
	Event OnSelectST()
		toggle_more_slots[11] = !toggle_more_slots[11]									;swap the current state of the toggle
		SetToggleOptionValueST(toggle_more_slots[11])
		If toggle_more_slots[11] == True												;if new toggle is on
		ignoremoreslots[11] = True														;do not unequip slot 11
		ElseIf toggle_more_slots[11] == False											;if new toggle is off
		ignoremoreslots[11] = False														;unequip slot 11
		EndIf
	EndEvent

	Event OnDefaultST()
		toggle_more_slots[11] = False													;default is off
		ignoremoreslots[11] = False														;default is unequip slot 11
		SetToggleOptionValueST(toggle_more_slots[11])									;set toggle to default (off)
	EndEvent

	Event OnHighlightST()
		SetInfoText("")																	;no highlight text
	EndEvent
EndState

State	more_slots42
	Event OnSelectST()
		toggle_more_slots[12] = !toggle_more_slots[12]									;swap the current state of the toggle
		SetToggleOptionValueST(toggle_more_slots[12])
		If toggle_more_slots[12] == True												;if new toggle is on
		ignoremoreslots[12] = True														;do not unequip slot 12
		ElseIf toggle_more_slots[12] == False											;if new toggle is off
		ignoremoreslots[12] = False														;unequip slot 12
		EndIf
	EndEvent

	Event OnDefaultST()
		toggle_more_slots[12] = False													;default is off
		ignoremoreslots[12] = False														;default is unequip slot 12
		SetToggleOptionValueST(toggle_more_slots[12])									;set toggle to default (off)
	EndEvent

	Event OnHighlightST()
		SetInfoText("")																	;no highlight text
	EndEvent
EndState

State	more_slots43
	Event OnSelectST()
		toggle_more_slots[13] = !toggle_more_slots[13]									;swap the current state of the toggle
		SetToggleOptionValueST(toggle_more_slots[13])
		If toggle_more_slots[13] == True												;if new toggle is on
		ignoremoreslots[13] = True														;do not unequip slot 13
		ElseIf toggle_more_slots[13] == False											;if new toggle is off
		ignoremoreslots[13] = False														;unequip slot 13
		EndIf
	EndEvent

	Event OnDefaultST()
		toggle_more_slots[13] = False													;default is off
		ignoremoreslots[13] = False														;default is unequip slot 13
		SetToggleOptionValueST(toggle_more_slots[13])									;set toggle to default (off)
	EndEvent

	Event OnHighlightST()
		SetInfoText("")																	;no highlight text
	EndEvent
EndState

State	more_slots44
	Event OnSelectST()
		toggle_more_slots[14] = !toggle_more_slots[14]									;swap the current state of the toggle
		SetToggleOptionValueST(toggle_more_slots[14])
		If toggle_more_slots[14] == True												;if new toggle is on
		ignoremoreslots[14] = True														;do not unequip slot 14
		ElseIf toggle_more_slots[14] == False											;if new toggle is off
		ignoremoreslots[14] = False														;unequip slot 14
		EndIf
	EndEvent

	Event OnDefaultST()
		toggle_more_slots[14] = False													;default is off
		ignoremoreslots[14] = False														;default is unequip slot 14
		SetToggleOptionValueST(toggle_more_slots[14])									;set toggle to default (off)
	EndEvent

	Event OnHighlightST()
		SetInfoText("")																	;no highlight text
	EndEvent
EndState

State	more_slots45
	Event OnSelectST()
		toggle_more_slots[15] = !toggle_more_slots[15]									;swap the current state of the toggle
		SetToggleOptionValueST(toggle_more_slots[15])
		If toggle_more_slots[15] == True												;if new toggle is on
		ignoremoreslots[15] = True														;do not unequip slot 15
		ElseIf toggle_more_slots[15] == False											;if new toggle is off
		ignoremoreslots[15] = False														;unequip slot 15
		EndIf
	EndEvent

	Event OnDefaultST()
		toggle_more_slots[15] = False													;default is off
		ignoremoreslots[15] = False														;default is unequip slot 15
		SetToggleOptionValueST(toggle_more_slots[15])									;set toggle to default (off)
	EndEvent

	Event OnHighlightST()
		SetInfoText("")																	;no highlight text
	EndEvent
EndState

State	more_slots46
	Event OnSelectST()
		toggle_more_slots[16] = !toggle_more_slots[16]									;swap the current state of the toggle
		SetToggleOptionValueST(toggle_more_slots[16])
		If toggle_more_slots[16] == True												;if new toggle is on
		ignoremoreslots[16] = True														;do not unequip slot 16
		ElseIf toggle_more_slots[16] == False											;if new toggle is off
		ignoremoreslots[16] = False														;unequip slot 16
		EndIf
	EndEvent

	Event OnDefaultST()
		toggle_more_slots[16] = False													;default is off
		ignoremoreslots[16] = False														;default is unequip slot 16
		SetToggleOptionValueST(toggle_more_slots[16])									;set toggle to default (off)
	EndEvent

	Event OnHighlightST()
		SetInfoText("")																	;no highlight text
	EndEvent
EndState

State	more_slots47
	Event OnSelectST()
		toggle_more_slots[17] = !toggle_more_slots[17]									;swap the current state of the toggle
		SetToggleOptionValueST(toggle_more_slots[17])
		If toggle_more_slots[17] == True												;if new toggle is on
		ignoremoreslots[17] = True														;do not unequip slot 17
		ElseIf toggle_more_slots[17] == False											;if new toggle is off
		ignoremoreslots[17] = False														;unequip slot 17
		EndIf
	EndEvent

	Event OnDefaultST()
		toggle_more_slots[17] = False													;default is off
		ignoremoreslots[17] = False														;default is unequip slot 17
		SetToggleOptionValueST(toggle_more_slots[17])									;set toggle to default (off)
	EndEvent

	Event OnHighlightST()
		SetInfoText("")																	;no highlight text
	EndEvent
EndState

State	more_slots48
	Event OnSelectST()
		toggle_more_slots[18] = !toggle_more_slots[18]									;swap the current state of the toggle
		SetToggleOptionValueST(toggle_more_slots[18])
		If toggle_more_slots[18] == True												;if new toggle is on
		ignoremoreslots[18] = True														;do not unequip slot 18
		ElseIf toggle_more_slots[18] == False											;if new toggle is off
		ignoremoreslots[18] = False														;unequip slot 18
		EndIf
	EndEvent

	Event OnDefaultST()
		toggle_more_slots[18] = False													;default is off
		ignoremoreslots[18] = False														;default is unequip slot 18
		SetToggleOptionValueST(toggle_more_slots[18])									;set toggle to default (off)
	EndEvent

	Event OnHighlightST()
		SetInfoText("")																	;no highlight text
	EndEvent
EndState

State	more_slots49
	Event OnSelectST()
		toggle_more_slots[19] = !toggle_more_slots[19]									;swap the current state of the toggle
		SetToggleOptionValueST(toggle_more_slots[19])
		If toggle_more_slots[19] == True												;if new toggle is on
		ignoremoreslots[19] = True														;do not unequip slot 19
		ElseIf toggle_more_slots[19] == False											;if new toggle is off
		ignoremoreslots[19] = False														;unequip slot 19
		EndIf
	EndEvent

	Event OnDefaultST()
		toggle_more_slots[19] = False													;default is off
		ignoremoreslots[19] = False														;default is unequip slot 19
		SetToggleOptionValueST(toggle_more_slots[19])									;set toggle to default (off)
	EndEvent

	Event OnHighlightST()
		SetInfoText("")																	;no highlight text
	EndEvent
EndState

State	more_slots50
	Event OnSelectST()
		toggle_more_slots[20] = !toggle_more_slots[20]									;swap the current state of the toggle
		SetToggleOptionValueST(toggle_more_slots[20])
		If toggle_more_slots[20] == True												;if new toggle is on
		ignoremoreslots[20] = True														;do not unequip slot 20
		ElseIf toggle_more_slots[20] == False											;if new toggle is off
		ignoremoreslots[20] = False														;unequip slot 20
		EndIf
	EndEvent

	Event OnDefaultST()
		toggle_more_slots[20] = False													;default is off
		ignoremoreslots[20] = False														;default is unequip slot 20
		SetToggleOptionValueST(toggle_more_slots[20])									;set toggle to default (off)
	EndEvent

	Event OnHighlightST()
		SetInfoText("")																	;no highlight text
	EndEvent
EndState

State	more_slots51
	Event OnSelectST()
		toggle_more_slots[21] = !toggle_more_slots[21]									;swap the current state of the toggle
		SetToggleOptionValueST(toggle_more_slots[21])
		If toggle_more_slots[21] == True												;if new toggle is on
		ignoremoreslots[21] = True														;do not unequip slot 21
		ElseIf toggle_more_slots[21] == False											;if new toggle is off
		ignoremoreslots[21] = False														;unequip slot 21
		EndIf
	EndEvent

	Event OnDefaultST()
		toggle_more_slots[21] = False													;default is off
		ignoremoreslots[21] = False														;default is unequip slot 21
		SetToggleOptionValueST(toggle_more_slots[21])									;set toggle to default (off)
	EndEvent

	Event OnHighlightST()
		SetInfoText("")																	;no highlight text
	EndEvent
EndState

State	more_slots52
	Event OnSelectST()
		toggle_more_slots[22] = !toggle_more_slots[22]									;swap the current state of the toggle
		SetToggleOptionValueST(toggle_more_slots[22])
		If toggle_more_slots[22] == True												;if new toggle is on
		ignoremoreslots[22] = True														;do not unequip slot 22
		ElseIf toggle_more_slots[22] == False											;if new toggle is off
		ignoremoreslots[22] = False														;unequip slot 22
		EndIf
	EndEvent

	Event OnDefaultST()
		toggle_more_slots[22] = False													;default is off
		ignoremoreslots[22] = False														;default is unequip slot 22
		SetToggleOptionValueST(toggle_more_slots[22])									;set toggle to default (off)
	EndEvent

	Event OnHighlightST()
		SetInfoText("")																	;no highlight text
	EndEvent
EndState

State	more_slots53
	Event OnSelectST()
		toggle_more_slots[23] = !toggle_more_slots[23]									;swap the current state of the toggle
		SetToggleOptionValueST(toggle_more_slots[23])
		If toggle_more_slots[23] == True												;if new toggle is on
		ignoremoreslots[23] = True														;do not unequip slot 23
		ElseIf toggle_more_slots[23] == False											;if new toggle is off
		ignoremoreslots[23] = False														;unequip slot 23
		EndIf
	EndEvent

	Event OnDefaultST()
		toggle_more_slots[23] = False													;default is off
		ignoremoreslots[23] = False														;default is unequip slot 23
		SetToggleOptionValueST(toggle_more_slots[23])									;set toggle to default (off)
	EndEvent

	Event OnHighlightST()
		SetInfoText("")																	;no highlight text
	EndEvent
EndState

State	more_slots54
	Event OnSelectST()
		toggle_more_slots[24] = !toggle_more_slots[24]									;swap the current state of the toggle
		SetToggleOptionValueST(toggle_more_slots[24])
		If toggle_more_slots[24] == True												;if new toggle is on
		ignoremoreslots[24] = True														;do not unequip slot 24
		ElseIf toggle_more_slots[24] == False											;if new toggle is off
		ignoremoreslots[24] = False														;unequip slot 24
		EndIf
	EndEvent

	Event OnDefaultST()
		toggle_more_slots[24] = False													;default is off
		ignoremoreslots[24] = False														;default is unequip slot 24
		SetToggleOptionValueST(toggle_more_slots[24])									;set toggle to default (off)
	EndEvent

	Event OnHighlightST()
		SetInfoText("")																	;no highlight text
	EndEvent
EndState

State	more_slots55
	Event OnSelectST()
		toggle_more_slots[25] = !toggle_more_slots[25]									;swap the current state of the toggle
		SetToggleOptionValueST(toggle_more_slots[25])
		If toggle_more_slots[25] == True												;if new toggle is on
		ignoremoreslots[25] = True														;do not unequip slot 25
		ElseIf toggle_more_slots[25] == False											;if new toggle is off
		ignoremoreslots[25] = False														;unequip slot 25
		EndIf
	EndEvent

	Event OnDefaultST()
		toggle_more_slots[25] = False													;default is off
		ignoremoreslots[25] = False														;default is unequip slot 25
		SetToggleOptionValueST(toggle_more_slots[25])									;set toggle to default (off)
	EndEvent

	Event OnHighlightST()
		SetInfoText("")																	;no highlight text
	EndEvent
EndState

State	more_slots56
	Event OnSelectST()
		toggle_more_slots[26] = !toggle_more_slots[26]									;swap the current state of the toggle
		SetToggleOptionValueST(toggle_more_slots[26])
		If toggle_more_slots[26] == True												;if new toggle is on
		ignoremoreslots[26] = True														;do not unequip slot 26
		ElseIf toggle_more_slots[26] == False											;if new toggle is off
		ignoremoreslots[26] = False														;unequip slot 26
		EndIf
	EndEvent

	Event OnDefaultST()
		toggle_more_slots[26] = False													;default is off
		ignoremoreslots[26] = False														;default is unequip slot 26
		SetToggleOptionValueST(toggle_more_slots[26])									;set toggle to default (off)
	EndEvent

	Event OnHighlightST()
		SetInfoText("")																	;no highlight text
	EndEvent
EndState

State	more_slots57
	Event OnSelectST()
		toggle_more_slots[27] = !toggle_more_slots[27]									;swap the current state of the toggle
		SetToggleOptionValueST(toggle_more_slots[27])
		If toggle_more_slots[27] == True												;if new toggle is on
		ignoremoreslots[27] = True														;do not unequip slot 27
		ElseIf toggle_more_slots[27] == False											;if new toggle is off
		ignoremoreslots[27] = False														;unequip slot 27
		EndIf
	EndEvent

	Event OnDefaultST()
		toggle_more_slots[27] = False													;default is off
		ignoremoreslots[27] = False														;default is unequip slot 27
		SetToggleOptionValueST(toggle_more_slots[27])									;set toggle to default (off)
	EndEvent

	Event OnHighlightST()
		SetInfoText("")																	;no highlight text
	EndEvent
EndState

State	more_slots58
	Event OnSelectST()
		toggle_more_slots[28] = !toggle_more_slots[28]									;swap the current state of the toggle
		SetToggleOptionValueST(toggle_more_slots[28])
		If toggle_more_slots[28] == True												;if new toggle is on
		ignoremoreslots[28] = True														;do not unequip slot 28
		ElseIf toggle_more_slots[28] == False											;if new toggle is off
		ignoremoreslots[28] = False														;unequip slot 28
		EndIf
	EndEvent

	Event OnDefaultST()
		toggle_more_slots[28] = False													;default is off
		ignoremoreslots[28] = False														;default is unequip slot 28
		SetToggleOptionValueST(toggle_more_slots[28])									;set toggle to default (off)
	EndEvent

	Event OnHighlightST()
		SetInfoText("")																	;no highlight text
	EndEvent
EndState

State	more_slots59
	Event OnSelectST()
		toggle_more_slots[29] = !toggle_more_slots[29]									;swap the current state of the toggle
		SetToggleOptionValueST(toggle_more_slots[29])
		If toggle_more_slots[29] == True												;if new toggle is on
		ignoremoreslots[29] = True														;do not unequip slot 29
		ElseIf toggle_more_slots[29] == False											;if new toggle is off
		ignoremoreslots[29] = False														;unequip slot 29
		EndIf
	EndEvent

	Event OnDefaultST()
		toggle_more_slots[29] = False													;default is off
		ignoremoreslots[29] = False														;default is unequip slot 29
		SetToggleOptionValueST(toggle_more_slots[29])									;set toggle to default (off)
	EndEvent

	Event OnHighlightST()
		SetInfoText("")																	;no highlight text
	EndEvent
EndState

State	more_slots60
	Event OnSelectST()
		toggle_more_slots[30] = !toggle_more_slots[30]									;swap the current state of the toggle
		SetToggleOptionValueST(toggle_more_slots[30])
		If toggle_more_slots[30] == True												;if new toggle is on
		ignoremoreslots[30] = True														;do not unequip slot 30
		ElseIf toggle_more_slots[30] == False											;if new toggle is off
		ignoremoreslots[30] = False														;unequip slot 30
		EndIf
	EndEvent

	Event OnDefaultST()
		toggle_more_slots[30] = False													;default is off
		ignoremoreslots[30] = False														;default is unequip slot 30
		SetToggleOptionValueST(toggle_more_slots[30])									;set toggle to default (off)
	EndEvent

	Event OnHighlightST()
		SetInfoText("")																	;no highlight text
	EndEvent
EndState

State	more_slots61
	Event OnSelectST()
		toggle_more_slots[31] = !toggle_more_slots[31]									;swap the current state of the toggle
		SetToggleOptionValueST(toggle_more_slots[31])
		If toggle_more_slots[31] == True												;if new toggle is on
		ignoremoreslots[31] = True														;do not unequip slot 31
		ElseIf toggle_more_slots[31] == False											;if new toggle is off
		ignoremoreslots[31] = False														;unequip slot 31
		EndIf
	EndEvent

	Event OnDefaultST()
		toggle_more_slots[30] = False													;default is off
		ignoremoreslots[30] = False														;default is unequip slot 30
		SetToggleOptionValueST(toggle_more_slots[30])									;set toggle to default (off)
	EndEvent

	Event OnHighlightST()
		SetInfoText("")																	;no highlight text
	EndEvent
EndState

State	more_npc_slots30
	Event OnSelectST()
		toggle_more_npc_slots[0] = !toggle_more_npc_slots[0]							;swap the current state of the toggle
		SetToggleOptionValueST(toggle_more_npc_slots[0])
		If toggle_more_npc_slots[0] == True												;if new toggle is on
		ignoremorenpcslots[0] = True													;do not unequip slot 0
		ElseIf toggle_more_npc_slots[0] == False										;if new toggle is off
		ignoremorenpcslots[0] = False													;unequip slot 0
		EndIf
		dz_booleans_check()
	EndEvent

	Event OnDefaultST()
		toggle_more_npc_slots[0] = False												;default is off
		ignoremorenpcslots[0] = False													;default is unequip slot 0
		SetToggleOptionValueST(toggle_more_npc_slots[0])								;set toggle to default (off)
	EndEvent

	Event OnHighlightST()
		SetInfoText("")																	;no highlight text
	EndEvent
EndState

State	more_npc_slots31
	Event OnSelectST()
		toggle_more_npc_slots[1] = !toggle_more_npc_slots[1]							;swap the current state of the toggle
		SetToggleOptionValueST(toggle_more_npc_slots[1])
		If toggle_more_npc_slots[1] == True												;if new toggle is on
		ignoremorenpcslots[1] = True													;do not unequip slot 1
		ElseIf toggle_more_npc_slots[1] == False										;if new toggle is off
		ignoremorenpcslots[1] = False													;unequip slot 1
		EndIf
		dz_booleans_check()
	EndEvent

	Event OnDefaultST()
		toggle_more_npc_slots[1] = False												;default is off
		ignoremorenpcslots[1] = False													;default is unequip slot 1
		SetToggleOptionValueST(toggle_more_npc_slots[1])								;set toggle to default (off)
	EndEvent

	Event OnHighlightST()
		SetInfoText("")																	;no highlight text
	EndEvent
EndState

State	more_npc_slots32
	Event OnSelectST()
		toggle_more_npc_slots[2] = !toggle_more_npc_slots[2]							;swap the current state of the toggle
		SetToggleOptionValueST(toggle_more_npc_slots[2])
		If toggle_more_npc_slots[2] == True												;if new toggle is on
		ignoremorenpcslots[2] = True													;do not unequip slot 2
		ElseIf toggle_more_npc_slots[2] == False										;if new toggle is off
		ignoremorenpcslots[2] = False													;unequip slot 2
		EndIf
		dz_booleans_check()
	EndEvent

	Event OnDefaultST()
		toggle_more_npc_slots[2] = False												;default is off
		ignoremorenpcslots[2] = False													;default is unequip slot 2
		SetToggleOptionValueST(toggle_more_npc_slots[2])								;set toggle to default (off)
	EndEvent

	Event OnHighlightST()
		SetInfoText("")																	;no highlight text
	EndEvent
EndState

State	more_npc_slots33
	Event OnSelectST()
		toggle_more_npc_slots[3] = !toggle_more_npc_slots[3]							;swap the current state of the toggle
		SetToggleOptionValueST(toggle_more_npc_slots[3])
		If toggle_more_npc_slots[3] == True											;if new toggle is on
		ignoremorenpcslots[3] = True													;do not unequip slot 3
		ElseIf toggle_more_npc_slots[3] == False										;if new toggle is off
		ignoremorenpcslots[3] = False													;unequip slot 3
		EndIf
		dz_booleans_check()
	EndEvent

	Event OnDefaultST()
		toggle_more_npc_slots[3] = False												;default is off
		ignoremorenpcslots[3] = False													;default is unequip slot 3
		SetToggleOptionValueST(toggle_more_npc_slots[3])								;set toggle to default (off)
	EndEvent

	Event OnHighlightST()
		SetInfoText("")																	;no highlight text
	EndEvent
EndState

State	more_npc_slots34
	Event OnSelectST()
		toggle_more_npc_slots[4] = !toggle_more_npc_slots[4]							;swap the current state of the toggle
		SetToggleOptionValueST(toggle_more_npc_slots[4])
		If toggle_more_npc_slots[4] == True												;if new toggle is on
		ignoremorenpcslots[4] = True													;do not unequip slot 4
		ElseIf toggle_more_npc_slots[4] == False										;if new toggle is off
		ignoremorenpcslots[4] = False													;unequip slot 4
		EndIf
		dz_booleans_check()
	EndEvent

	Event OnDefaultST()
		toggle_more_npc_slots[4] = False												;default is off
		ignoremorenpcslots[4] = False													;default is unequip slot 4
		SetToggleOptionValueST(toggle_more_npc_slots[4])								;set toggle to default (off)
	EndEvent

	Event OnHighlightST()
		SetInfoText("")																	;no highlight text
	EndEvent
EndState

State	more_npc_slots35
	Event OnSelectST()
		toggle_more_npc_slots[5] = !toggle_more_npc_slots[5]							;swap the current state of the toggle
		SetToggleOptionValueST(toggle_more_npc_slots[5])
		If toggle_more_npc_slots[5] == True												;if new toggle is on
		ignoremorenpcslots[5] = True													;do not unequip slot 5
		ElseIf toggle_more_npc_slots[5] == False										;if new toggle is off
		ignoremorenpcslots[5] = False													;unequip slot 5
		EndIf
		dz_booleans_check()
	EndEvent

	Event OnDefaultST()
		toggle_more_npc_slots[5] = False												;default is off
		ignoremorenpcslots[5] = False													;default is unequip slot 5
		SetToggleOptionValueST(toggle_more_npc_slots[5])								;set toggle to default (off)
	EndEvent

	Event OnHighlightST()
		SetInfoText("")																	;no highlight text
	EndEvent
EndState

State	more_npc_slots36
	Event OnSelectST()
		toggle_more_npc_slots[6] = !toggle_more_npc_slots[6]							;swap the current state of the toggle
		SetToggleOptionValueST(toggle_more_npc_slots[6])
		If toggle_more_npc_slots[6] == True												;if new toggle is on
		ignoremorenpcslots[6] = True													;do not unequip slot 6
		ElseIf toggle_more_npc_slots[6] == False										;if new toggle is off
		ignoremorenpcslots[6] = False													;unequip slot 6
		EndIf
		dz_booleans_check()
	EndEvent

	Event OnDefaultST()
		toggle_more_npc_slots[6] = False												;default is off
		ignoremorenpcslots[6] = False													;default is unequip slot 6
		SetToggleOptionValueST(toggle_more_npc_slots[6])								;set toggle to default (off)
	EndEvent

	Event OnHighlightST()
		SetInfoText("")																	;no highlight text
	EndEvent
EndState

State	more_npc_slots37
	Event OnSelectST()
		toggle_more_npc_slots[7] = !toggle_more_npc_slots[7]							;swap the current state of the toggle
		SetToggleOptionValueST(toggle_more_npc_slots[7])
		If toggle_more_npc_slots[7] == True												;if new toggle is on
		ignoremorenpcslots[7] = True													;do not unequip slot 7
		ElseIf toggle_more_npc_slots[7] == False										;if new toggle is off
		ignoremorenpcslots[7] = False													;unequip slot 7
		EndIf
		dz_booleans_check()
	EndEvent

	Event OnDefaultST()
		toggle_more_npc_slots[7] = False												;default is off
		ignoremorenpcslots[7] = False													;default is unequip slot 7
		SetToggleOptionValueST(toggle_more_npc_slots[7])								;set toggle to default (off)
	EndEvent

	Event OnHighlightST()
		SetInfoText("")																	;no highlight text
	EndEvent
EndState

State	more_npc_slots38
	Event OnSelectST()
		toggle_more_npc_slots[8] = !toggle_more_npc_slots[8]							;swap the current state of the toggle
		SetToggleOptionValueST(toggle_more_npc_slots[8])
		If toggle_more_npc_slots[8] == True												;if new toggle is on
		ignoremorenpcslots[8] = True													;do not unequip slot 8
		ElseIf toggle_more_npc_slots[8] == False										;if new toggle is off
		ignoremorenpcslots[8] = False													;unequip slot 8
		EndIf
		dz_booleans_check()
	EndEvent

	Event OnDefaultST()
		toggle_more_npc_slots[8] = False												;default is off
		ignoremorenpcslots[8] = False													;default is unequip slot 8
		SetToggleOptionValueST(toggle_more_npc_slots[8])								;set toggle to default (off)
	EndEvent

	Event OnHighlightST()
		SetInfoText("")																	;no highlight text
	EndEvent
EndState

State	more_npc_slots39
	Event OnSelectST()
		toggle_more_npc_slots[9] = !toggle_more_npc_slots[9]							;swap the current state of the toggle
		SetToggleOptionValueST(toggle_more_npc_slots[9])
		If toggle_more_npc_slots[9] == True												;if new toggle is on
		ignoremorenpcslots[9] = True													;do not unequip slot 9
		ElseIf toggle_more_npc_slots[9] == False										;if new toggle is off
		ignoremorenpcslots[9] = False													;unequip slot 9
		EndIf
		dz_booleans_check()
	EndEvent

	Event OnDefaultST()
		toggle_more_npc_slots[9] = False												;default is off
		ignoremorenpcslots[9] = False													;default is unequip slot 9
		SetToggleOptionValueST(toggle_more_npc_slots[9])								;set toggle to default (off)
	EndEvent

	Event OnHighlightST()
		SetInfoText("")																	;no highlight text
	EndEvent
EndState

State	more_npc_slots40
	Event OnSelectST()
		toggle_more_npc_slots[10] = !toggle_more_npc_slots[10]							;swap the current state of the toggle
		SetToggleOptionValueST(toggle_more_npc_slots[10])
		If toggle_more_npc_slots[10] == True											;if new toggle is on
		ignoremorenpcslots[10] = True													;do not unequip slot 10
		ElseIf toggle_more_npc_slots[10] == False										;if new toggle is off
		ignoremorenpcslots[10] = False													;unequip slot 10
		EndIf
		dz_booleans_check()
	EndEvent

	Event OnDefaultST()
		toggle_more_npc_slots[10] = False												;default is off
		ignoremorenpcslots[10] = False													;default is unequip slot 10
		SetToggleOptionValueST(toggle_more_npc_slots[10])								;set toggle to default (off)
	EndEvent

	Event OnHighlightST()
		SetInfoText("")																	;no highlight text
	EndEvent
EndState

State	more_npc_slots41
	Event OnSelectST()
		toggle_more_npc_slots[11] = !toggle_more_npc_slots[11]							;swap the current state of the toggle
		SetToggleOptionValueST(toggle_more_npc_slots[11])
		If toggle_more_npc_slots[11] == True											;if new toggle is on
		ignoremorenpcslots[11] = True													;do not unequip slot 11
		ElseIf toggle_more_npc_slots[11] == False										;if new toggle is off
		ignoremorenpcslots[11] = False													;unequip slot 11
		EndIf
		dz_booleans_check()
	EndEvent

	Event OnDefaultST()
		toggle_more_npc_slots[11] = False												;default is off
		ignoremorenpcslots[11] = False													;default is unequip slot 11
		SetToggleOptionValueST(toggle_more_npc_slots[11])								;set toggle to default (off)
	EndEvent

	Event OnHighlightST()
		SetInfoText("")																	;no highlight text
	EndEvent
EndState

State	more_npc_slots42
	Event OnSelectST()
		toggle_more_npc_slots[12] = !toggle_more_npc_slots[12]							;swap the current state of the toggle
		SetToggleOptionValueST(toggle_more_npc_slots[12])
		If toggle_more_npc_slots[12] == True											;if new toggle is on
		ignoremorenpcslots[12] = True													;do not unequip slot 12
		ElseIf toggle_more_npc_slots[12] == False										;if new toggle is off
		ignoremorenpcslots[12] = False													;unequip slot 12
		EndIf
		dz_booleans_check()
	EndEvent

	Event OnDefaultST()
		toggle_more_npc_slots[12] = False												;default is off
		ignoremorenpcslots[12] = False													;default is unequip slot 12
		SetToggleOptionValueST(toggle_more_npc_slots[12])								;set toggle to default (off)
	EndEvent

	Event OnHighlightST()
		SetInfoText("")																	;no highlight text
	EndEvent
EndState

State	more_npc_slots43
	Event OnSelectST()
		toggle_more_npc_slots[13] = !toggle_more_npc_slots[13]							;swap the current state of the toggle
		SetToggleOptionValueST(toggle_more_npc_slots[13])
		If toggle_more_npc_slots[13] == True											;if new toggle is on
		ignoremorenpcslots[13] = True													;do not unequip slot 13
		ElseIf toggle_more_npc_slots[13] == False										;if new toggle is off
		ignoremorenpcslots[13] = False													;unequip slot 13
		EndIf
		dz_booleans_check()
	EndEvent

	Event OnDefaultST()
		toggle_more_npc_slots[13] = False												;default is off
		ignoremorenpcslots[13] = False													;default is unequip slot 13
		SetToggleOptionValueST(toggle_more_npc_slots[13])								;set toggle to default (off)
	EndEvent

	Event OnHighlightST()
		SetInfoText("")																	;no highlight text
	EndEvent
EndState

State	more_npc_slots44
	Event OnSelectST()
		toggle_more_npc_slots[14] = !toggle_more_npc_slots[14]							;swap the current state of the toggle
		SetToggleOptionValueST(toggle_more_npc_slots[14])
		If toggle_more_npc_slots[14] == True											;if new toggle is on
		ignoremorenpcslots[14] = True													;do not unequip slot 14
		ElseIf toggle_more_npc_slots[14] == False										;if new toggle is off
		ignoremorenpcslots[14] = False													;unequip slot 14
		EndIf
		dz_booleans_check()
	EndEvent

	Event OnDefaultST()
		toggle_more_npc_slots[14] = False												;default is off
		ignoremorenpcslots[14] = False													;default is unequip slot 14
		SetToggleOptionValueST(toggle_more_npc_slots[14])								;set toggle to default (off)
	EndEvent

	Event OnHighlightST()
		SetInfoText("")																	;no highlight text
	EndEvent
EndState

State	more_npc_slots45
	Event OnSelectST()
		toggle_more_npc_slots[15] = !toggle_more_npc_slots[15]							;swap the current state of the toggle
		SetToggleOptionValueST(toggle_more_npc_slots[15])
		If toggle_more_npc_slots[15] == True											;if new toggle is on
		ignoremorenpcslots[15] = True													;do not unequip slot 15
		ElseIf toggle_more_npc_slots[15] == False										;if new toggle is off
		ignoremorenpcslots[15] = False													;unequip slot 15
		EndIf
		dz_booleans_check()
	EndEvent

	Event OnDefaultST()
		toggle_more_npc_slots[15] = False												;default is off
		ignoremorenpcslots[15] = False													;default is unequip slot 15
		SetToggleOptionValueST(toggle_more_npc_slots[15])								;set toggle to default (off)
	EndEvent

	Event OnHighlightST()
		SetInfoText("")																	;no highlight text
	EndEvent
EndState

State	more_npc_slots46
	Event OnSelectST()
		toggle_more_npc_slots[16] = !toggle_more_npc_slots[16]							;swap the current state of the toggle
		SetToggleOptionValueST(toggle_more_npc_slots[16])
		If toggle_more_npc_slots[16] == True											;if new toggle is on
		ignoremorenpcslots[16] = True													;do not unequip slot 16
		ElseIf toggle_more_npc_slots[16] == False										;if new toggle is off
		ignoremorenpcslots[16] = False													;unequip slot 16
		EndIf
		dz_booleans_check()
	EndEvent

	Event OnDefaultST()
		toggle_more_npc_slots[16] = False												;default is off
		ignoremorenpcslots[16] = False													;default is unequip slot 16
		SetToggleOptionValueST(toggle_more_npc_slots[16])								;set toggle to default (off)
	EndEvent

	Event OnHighlightST()
		SetInfoText("")																	;no highlight text
	EndEvent
EndState

State	more_npc_slots47
	Event OnSelectST()
		toggle_more_npc_slots[17] = !toggle_more_npc_slots[17]							;swap the current state of the toggle
		SetToggleOptionValueST(toggle_more_npc_slots[17])
		If toggle_more_npc_slots[17] == True											;if new toggle is on
		ignoremorenpcslots[17] = True													;do not unequip slot 17
		ElseIf toggle_more_npc_slots[17] == False										;if new toggle is off
		ignoremorenpcslots[17] = False													;unequip slot 17
		EndIf
		dz_booleans_check()
	EndEvent

	Event OnDefaultST()
		toggle_more_npc_slots[17] = False												;default is off
		ignoremorenpcslots[17] = False													;default is unequip slot 17
		SetToggleOptionValueST(toggle_more_npc_slots[17])								;set toggle to default (off)
	EndEvent

	Event OnHighlightST()
		SetInfoText("")																	;no highlight text
	EndEvent
EndState

State	more_npc_slots48
	Event OnSelectST()
		toggle_more_npc_slots[18] = !toggle_more_npc_slots[18]							;swap the current state of the toggle
		SetToggleOptionValueST(toggle_more_npc_slots[18])
		If toggle_more_npc_slots[18] == True											;if new toggle is on
		ignoremorenpcslots[18] = True													;do not unequip slot 18
		ElseIf toggle_more_npc_slots[18] == False										;if new toggle is off
		ignoremorenpcslots[18] = False													;unequip slot 18
		EndIf
		dz_booleans_check()
	EndEvent

	Event OnDefaultST()
		toggle_more_npc_slots[18] = False												;default is off
		ignoremorenpcslots[18] = False													;default is unequip slot 18
		SetToggleOptionValueST(toggle_more_npc_slots[18])								;set toggle to default (off)
	EndEvent

	Event OnHighlightST()
		SetInfoText("")																	;no highlight text
	EndEvent
EndState

State	more_npc_slots49
	Event OnSelectST()
		toggle_more_npc_slots[19] = !toggle_more_npc_slots[19]							;swap the current state of the toggle
		SetToggleOptionValueST(toggle_more_npc_slots[19])
		If toggle_more_npc_slots[19] == True											;if new toggle is on
		ignoremorenpcslots[19] = True													;do not unequip slot 19
		ElseIf toggle_more_npc_slots[19] == False										;if new toggle is off
		ignoremorenpcslots[19] = False													;unequip slot 19
		EndIf
		dz_booleans_check()
	EndEvent

	Event OnDefaultST()
		toggle_more_npc_slots[19] = False												;default is off
		ignoremorenpcslots[19] = False													;default is unequip slot 19
		SetToggleOptionValueST(toggle_more_npc_slots[19])								;set toggle to default (off)
	EndEvent

	Event OnHighlightST()
		SetInfoText("")																	;no highlight text
	EndEvent
EndState

State	more_npc_slots50
	Event OnSelectST()
		toggle_more_npc_slots[20] = !toggle_more_npc_slots[20]							;swap the current state of the toggle
		SetToggleOptionValueST(toggle_more_npc_slots[20])
		If toggle_more_npc_slots[20] == True											;if new toggle is on
		ignoremorenpcslots[20] = True													;do not unequip slot 20
		ElseIf toggle_more_npc_slots[20] == False										;if new toggle is off
		ignoremorenpcslots[20] = False													;unequip slot 20
		EndIf
		dz_booleans_check()
	EndEvent

	Event OnDefaultST()
		toggle_more_npc_slots[20] = False												;default is off
		ignoremorenpcslots[20] = False													;default is unequip slot 20
		SetToggleOptionValueST(toggle_more_npc_slots[20])								;set toggle to default (off)
	EndEvent

	Event OnHighlightST()
		SetInfoText("")																	;no highlight text
	EndEvent
EndState

State	more_npc_slots51
	Event OnSelectST()
		toggle_more_npc_slots[21] = !toggle_more_npc_slots[21]							;swap the current state of the toggle
		SetToggleOptionValueST(toggle_more_npc_slots[21])
		If toggle_more_npc_slots[21] == True											;if new toggle is on
		ignoremorenpcslots[21] = True													;do not unequip slot 21
		ElseIf toggle_more_npc_slots[21] == False										;if new toggle is off
		ignoremorenpcslots[21] = False													;unequip slot 21
		EndIf
		dz_booleans_check()
	EndEvent

	Event OnDefaultST()
		toggle_more_npc_slots[21] = False												;default is off
		ignoremorenpcslots[21] = False													;default is unequip slot 21
		SetToggleOptionValueST(toggle_more_npc_slots[21])								;set toggle to default (off)
	EndEvent

	Event OnHighlightST()
		SetInfoText("")																	;no highlight text
	EndEvent
EndState

State	more_npc_slots52
	Event OnSelectST()
		toggle_more_npc_slots[22] = !toggle_more_npc_slots[22]							;swap the current state of the toggle
		SetToggleOptionValueST(toggle_more_npc_slots[22])
		If toggle_more_npc_slots[22] == True											;if new toggle is on
		ignoremorenpcslots[22] = True													;do not unequip slot 22
		ElseIf toggle_more_npc_slots[22] == False										;if new toggle is off
		ignoremorenpcslots[22] = False													;unequip slot 22
		EndIf
		dz_booleans_check()
	EndEvent

	Event OnDefaultST()
		toggle_more_npc_slots[22] = False												;default is off
		ignoremorenpcslots[22] = False													;default is unequip slot 22
		SetToggleOptionValueST(toggle_more_npc_slots[22])								;set toggle to default (off)
	EndEvent

	Event OnHighlightST()
		SetInfoText("")																	;no highlight text
	EndEvent
EndState

State	more_npc_slots53
	Event OnSelectST()
		toggle_more_npc_slots[23] = !toggle_more_npc_slots[23]							;swap the current state of the toggle
		SetToggleOptionValueST(toggle_more_npc_slots[23])
		If toggle_more_npc_slots[23] == True											;if new toggle is on
		ignoremorenpcslots[23] = True													;do not unequip slot 23
		ElseIf toggle_more_npc_slots[23] == False										;if new toggle is off
		ignoremorenpcslots[23] = False													;unequip slot 23
		EndIf
		dz_booleans_check()
	EndEvent

	Event OnDefaultST()
		toggle_more_npc_slots[23] = False												;default is off
		ignoremorenpcslots[23] = False													;default is unequip slot 23
		SetToggleOptionValueST(toggle_more_npc_slots[23])								;set toggle to default (off)
	EndEvent

	Event OnHighlightST()
		SetInfoText("")																	;no highlight text
	EndEvent
EndState

State	more_npc_slots54
	Event OnSelectST()
		toggle_more_npc_slots[24] = !toggle_more_npc_slots[24]							;swap the current state of the toggle
		SetToggleOptionValueST(toggle_more_npc_slots[24])
		If toggle_more_npc_slots[24] == True											;if new toggle is on
		ignoremorenpcslots[24] = True													;do not unequip slot 24
		ElseIf toggle_more_npc_slots[24] == False										;if new toggle is off
		ignoremorenpcslots[24] = False													;unequip slot 24
		EndIf
		dz_booleans_check()
	EndEvent

	Event OnDefaultST()
		toggle_more_npc_slots[24] = False												;default is off
		ignoremorenpcslots[24] = False													;default is unequip slot 24
		SetToggleOptionValueST(toggle_more_npc_slots[24])								;set toggle to default (off)
	EndEvent

	Event OnHighlightST()
		SetInfoText("")																	;no highlight text
	EndEvent
EndState

State	more_npc_slots55
	Event OnSelectST()
		toggle_more_npc_slots[25] = !toggle_more_npc_slots[25]							;swap the current state of the toggle
		SetToggleOptionValueST(toggle_more_npc_slots[25])
		If toggle_more_npc_slots[25] == True											;if new toggle is on
		ignoremorenpcslots[25] = True													;do not unequip slot 25
		ElseIf toggle_more_npc_slots[25] == False										;if new toggle is off
		ignoremorenpcslots[25] = False													;unequip slot 25
		EndIf
		dz_booleans_check()
	EndEvent

	Event OnDefaultST()
		toggle_more_npc_slots[25] = False												;default is off
		ignoremorenpcslots[25] = False													;default is unequip slot 25
		SetToggleOptionValueST(toggle_more_npc_slots[25])								;set toggle to default (off)
	EndEvent

	Event OnHighlightST()
		SetInfoText("")																	;no highlight text
	EndEvent
EndState

State	more_npc_slots56
	Event OnSelectST()
		toggle_more_npc_slots[26] = !toggle_more_npc_slots[26]							;swap the current state of the toggle
		SetToggleOptionValueST(toggle_more_npc_slots[26])
		If toggle_more_npc_slots[26] == True											;if new toggle is on
		ignoremorenpcslots[26] = True													;do not unequip slot 26
		ElseIf toggle_more_npc_slots[26] == False										;if new toggle is off
		ignoremorenpcslots[26] = False													;unequip slot 26
		EndIf
		dz_booleans_check()
	EndEvent

	Event OnDefaultST()
		toggle_more_npc_slots[26] = False												;default is off
		ignoremorenpcslots[26] = False													;default is unequip slot 26
		SetToggleOptionValueST(toggle_more_npc_slots[26])								;set toggle to default (off)
	EndEvent

	Event OnHighlightST()
		SetInfoText("")																	;no highlight text
	EndEvent
EndState

State	more_npc_slots57
	Event OnSelectST()
		toggle_more_npc_slots[27] = !toggle_more_npc_slots[27]							;swap the current state of the toggle
		SetToggleOptionValueST(toggle_more_npc_slots[27])
		If toggle_more_npc_slots[27] == True											;if new toggle is on
		ignoremorenpcslots[27] = True													;do not unequip slot 27
		ElseIf toggle_more_npc_slots[27] == False										;if new toggle is off
		ignoremorenpcslots[27] = False													;unequip slot 27
		EndIf
		dz_booleans_check()
	EndEvent

	Event OnDefaultST()
		toggle_more_npc_slots[27] = False												;default is off
		ignoremorenpcslots[27] = False													;default is unequip slot 27
		SetToggleOptionValueST(toggle_more_npc_slots[27])								;set toggle to default (off)
	EndEvent

	Event OnHighlightST()
		SetInfoText("")																	;no highlight text
	EndEvent
EndState

State	more_npc_slots58
	Event OnSelectST()
		toggle_more_npc_slots[28] = !toggle_more_npc_slots[28]							;swap the current state of the toggle
		SetToggleOptionValueST(toggle_more_npc_slots[28])
		If toggle_more_npc_slots[28] == True											;if new toggle is on
		ignoremorenpcslots[28] = True													;do not unequip slot 28
		ElseIf toggle_more_npc_slots[28] == False										;if new toggle is off
		ignoremorenpcslots[28] = False													;unequip slot 28
		EndIf
		dz_booleans_check()
	EndEvent

	Event OnDefaultST()
		toggle_more_npc_slots[28] = False												;default is off
		ignoremorenpcslots[28] = False													;default is unequip slot 28
		SetToggleOptionValueST(toggle_more_npc_slots[28])								;set toggle to default (off)
	EndEvent

	Event OnHighlightST()
		SetInfoText("")																	;no highlight text
	EndEvent
EndState

State	more_npc_slots59
	Event OnSelectST()
		toggle_more_npc_slots[29] = !toggle_more_npc_slots[29]							;swap the current state of the toggle
		SetToggleOptionValueST(toggle_more_npc_slots[29])
		If toggle_more_npc_slots[29] == True											;if new toggle is on
		ignoremorenpcslots[29] = True													;do not unequip slot 29
		ElseIf toggle_more_npc_slots[29] == False										;if new toggle is off
		ignoremorenpcslots[29] = False													;unequip slot 29
		EndIf
		dz_booleans_check()
	EndEvent

	Event OnDefaultST()
		toggle_more_npc_slots[29] = False												;default is off
		ignoremorenpcslots[29] = False													;default is unequip slot 29
		SetToggleOptionValueST(toggle_more_npc_slots[29])								;set toggle to default (off)
	EndEvent

	Event OnHighlightST()
		SetInfoText("")																	;no highlight text
	EndEvent
EndState

State	more_npc_slots60
	Event OnSelectST()
		toggle_more_npc_slots[30] = !toggle_more_npc_slots[30]							;swap the current state of the toggle
		SetToggleOptionValueST(toggle_more_npc_slots[30])
		If toggle_more_npc_slots[30] == True											;if new toggle is on
		ignoremorenpcslots[30] = True													;do not unequip slot 30
		ElseIf toggle_more_npc_slots[30] == False										;if new toggle is off
		ignoremorenpcslots[30] = False													;unequip slot 30
		EndIf
		dz_booleans_check()
	EndEvent

	Event OnDefaultST()
		toggle_more_npc_slots[30] = False												;default is off
		ignoremorenpcslots[30] = False													;default is unequip slot 30
		SetToggleOptionValueST(toggle_more_npc_slots[30])								;set toggle to default (off)
	EndEvent

	Event OnHighlightST()
		SetInfoText("")																	;no highlight text
	EndEvent
EndState

State	more_npc_slots61
	Event OnSelectST()
		toggle_more_npc_slots[31] = !toggle_more_npc_slots[31]							;swap the current state of the toggle
		SetToggleOptionValueST(toggle_more_npc_slots[31])
		If toggle_more_npc_slots[31] == True											;if new toggle is on
		ignoremorenpcslots[31] = True													;do not unequip slot 31
		ElseIf toggle_more_npc_slots[31] == False										;if new toggle is off
		ignoremorenpcslots[31] = False													;unequip slot 31
		EndIf
		dz_booleans_check()
	EndEvent

	Event OnDefaultST()
		toggle_more_npc_slots[31] = False												;default is off
		ignoremorenpcslots[31] = False													;default is unequip slot 31
		SetToggleOptionValueST(toggle_more_npc_slots[31])								;set toggle to default (off)
	EndEvent

	Event OnHighlightST()
		SetInfoText("")																	;no highlight text
	EndEvent
EndState

State	state_debug
	Event OnSelectST()
		toggle_debug = !toggle_debug													;swap the current state of the toggle
		SetToggleOptionValueST(toggle_debug)
	EndEvent

	Event OnDefaultST()
		toggle_debug = False															;default is off
		SetToggleOptionValueST(toggle_debug)											;set toggle to default (off)
	EndEvent

	Event OnHighlightST()
		SetInfoText("$debug_highlight_textinfo")										;no highlight text
	EndEvent
EndState

;;;new function for debugging;;;
Function dz_booleans_check()
	String msg = ("toggle_hair = "+toggle_hair+" | "+"toggle_longhair = "+toggle_longhair+" | "+"toggle_circlet = "+toggle_circlet+" | "+"toggle_npc_hair = "+toggle_npc_hair+" | "+"toggle_npc_longhair = "+toggle_npc_longhair+" | "+"toggle_npc_circlet = "+toggle_npc_circlet+" | "+"toggle_npc_combat = "+toggle_npc_combat+" | "+"toggle_player_slot_choice = "+toggle_player_slot_choice+" | "+"toggle_npc_slot_choice = "+toggle_npc_slot_choice+" | "+"toggle_more_slot_options = "+toggle_more_slot_options+" | "+"toggle_more_slots = "+toggle_more_slots+" | "+"toggle_more_npc_slots = "+toggle_more_npc_slots)
	DEBUG_TRACE(NONE,msg)
	
	
	
	
	
	
	
	
	
	
EndFunction

;;;functions for using cleaning mods;;;
Function dz_player_clean_function(Actor triggerRef)
	If toggle_allowplayer == False														;no cleaning function if player undressing not set
		DEBUG_TRACE(triggerRef," dz_player_clean_function: player not allowed to undress - exiting.")
		Return
	EndIf
	If dz_clean_mods == False || triggerRef.IsSwimming()								;do not use a clean mod if none intsalled or swimming
		DEBUG_TRACE(triggerRef," dz_player_clean_function:is swimming or no clean mods detected")
		Return
	ElseIf cleanmod == "DAB" && toggle_use_dab_clean_mod == True
		DEBUG_TRACE(triggerRef," dz_player_clean_function: cleanmod is DAB, waiting then casting spell on player")
		Utility.Wait(player_clean_delay)
		dz_dab_player_clean_spell.Cast(PlayerRef, triggerRef)							;Dirt and Blood operates using spells
		Return																			;we are done,exit the function
	Elseif cleanmod == "BIS" && toggle_use_bis_clean_mod == True
		DEBUG_TRACE(triggerRef," dz_player_clean_function: cleanmod is BIS, waiting then calling function on player")
		Utility.Wait(player_clean_delay)
		bis_cleanscript.TryBatheActor(PlayerRef,None)									;Bathing in Skyrim does not use spells
	EndIf
EndFunction

Function dz_npc_clean_function(Actor triggerRef)
	If toggle_allowNPC == False															;no cleaning function if no NPC undressing
		DEBUG_TRACE(triggerRef, "dz_npc_clean_function: NPCs don't undress - exiting.")
		Return
	EndIf
	If dz_clean_mods == False || triggerRef.IsSwimming()								;do not use a clean mod if none installed or swimming
		DEBUG_TRACE(triggerRef," dz_npc_clean_function: is swimming or no clean mods detected")
		Return
	ElseIf cleanmod == "DAB" && toggle_npc_use_dab_clean_mod == True
		DEBUG_TRACE(triggerRef," dz_npc_clean_function: cleanmod is DAB, waiting then casting spell on NPC")
		Utility.Wait(npc_clean_delay)
		dz_dab_npc_clean_spell.Cast(PlayerRef, triggerRef)								;Dirt and Blood operates using spells
		Return																			;we are done,exit the function
	Elseif cleanmod == "BIS" && toggle_npc_use_bis_clean_mod == True
		DEBUG_TRACE(triggerRef," dz_npc_clean_function: cleanmod is BIS, waiting then calling function on NPC")
		Utility.Wait(npc_clean_delay)
		bis_cleanscript.TryBatheActor(triggerRef,None)									;Bathing in Skyrim does not use spells
	EndIf
EndFunction
;;;end of functions for using cleaning mods;;;

Function dz_maintenance()																;runs on mod install and playerloadgame event (from player alias script)
	DEBUG_TRACE(None,"Maintenance function started...")
	If Game.IsPluginInstalled("Dirt and Blood - Dynamic Visuals.esp")
		DEBUG_TRACE(None,"Dirt and Blood mod found")
		dz_clean_mods = True
		cleanmod = "DAB"
		STATEPLAYERCLEANWAITOPTION = OPTION_FLAG_NONE
		STATENPCCLEANWAITOPTION = OPTION_FLAG_NONE
		DABCLEANMODSTATEOPTION = OPTION_FLAG_NONE
		DABNPCCLEANMODSTATEOPTION = OPTION_FLAG_NONE
		BISCLEANMODSTATEOPTION = OPTION_FLAG_DISABLED
		BISNPCCLEANMODSTATEOPTION = OPTION_FLAG_DISABLED
		dz_dab_player_clean_spell = Game.GetFormFromFile(0x0000082F,"Dirt and Blood - Dynamic Visuals.esp") As Spell ;DirtyClean_YoSelf spell from this mod
		DEBUG_TRACE(None," DAB player clean spell is "+dz_dab_player_clean_spell)
		DEBUG_TRACE(None," DAB player clean spell name is "+dz_dab_player_clean_spell.GetName())
		dz_dab_NPC_clean_spell = Game.GetFormFromFile(0x00000860,"Dirt and Blood - Dynamic Visuals.esp") As Spell ;DirtyClean_YoSelfNPC spell from this mod
		DEBUG_TRACE(None," DAB NPC clean spell is "+dz_dab_NPC_clean_spell)
		DEBUG_TRACE(None," DAB NPC clean spell name is "+dz_dab_NPC_clean_spell.GetName())
		DEBUG_TRACE(None,"Maintenance function finished")
	ElseIf Game.IsPluginInstalled("Bathing In Skyrim - Main.esp")
		DEBUG_TRACE(None,"Bathing In Skyrim mod found")
		dz_clean_mods = True
		cleanmod = "BIS"
		STATEPLAYERCLEANWAITOPTION = OPTION_FLAG_NONE
		STATENPCCLEANWAITOPTION = OPTION_FLAG_NONE
		BISCLEANMODSTATEOPTION = OPTION_FLAG_NONE
		BISNPCCLEANMODSTATEOPTION = OPTION_FLAG_NONE
		DABCLEANMODSTATEOPTION = OPTION_FLAG_DISABLED
		DABNPCCLEANMODSTATEOPTION = OPTION_FLAG_DISABLED
		bis_cleanscript = Game.GetFormFromFile(0x000279ED,"Bathing In Skyrim - Main.esp") As mzinBatheQuest
		DEBUG_TRACE(None,"Maintenance function finished")
	Else
		dz_clean_mods == False
		DABCLEANMODSTATEOPTION = OPTION_FLAG_DISABLED
		DABNPCCLEANMODSTATEOPTION = OPTION_FLAG_DISABLED
		BISCLEANMODSTATEOPTION = OPTION_FLAG_DISABLED
		BISNPCCLEANMODSTATEOPTION = OPTION_FLAG_DISABLED
		STATEPLAYERCLEANWAITOPTION = OPTION_FLAG_DISABLED
		STATENPCCLEANWAITOPTION = OPTION_FLAG_DISABLED
		DEBUG_TRACE(None,"Maintenance function finished - no clean mods found")
	EndIf
EndFunction

Function dz_more_slot_options_on()
	optionsenable = OPTION_FLAG_NONE
			Int i
			i = 30
			While i < 62
			String j = i As String
				SetOptionFlagsST(optionsenable,False,"more_slots"+j)
				SetOptionFlagsST(optionsenable,False,"more_npc_slots"+j)
				i = i + 1
			EndWhile
			toggle_player_slot_choice = False
			toggle_npc_slot_choice = False
			dztoggle_player_slots_off()
			dztoggle_npc_slots_off()
EndFunction

Function dz_more_slot_options_off()
	optionsenable = OPTION_FLAG_DISABLED
			Int i
			i = 30
			While i < 62
			String j = i As String
				SetToggleOptionValueST(False,False,"more_slots"+j)
				SetToggleOptionValueST(False,False,"more_npc_slots"+j)
				SetOptionFlagsST(OPTION_FLAG_DISABLED,False,"more_slots"+j)
				SetOptionFlagsST(optionsenable,False,"more_npc_slots"+j)
				toggle_more_slots[i - 30] = False
				toggle_more_npc_slots[i - 30] = False
				i = i + 1
			EndWhile
EndFunction

Function dztoggle_player_slots_on()
	playerchoice = OPTION_FLAG_NONE
		SetOptionFlagsST(playerchoice,False,"slot31option")								;ungrey slot option
		SetOptionFlagsST(playerchoice,False,"slot41option")								;ungrey slot option
		SetOptionFlagsST(playerchoice,False,"slot42option")								;ungrey slot option
		optionsenable = OPTION_FLAG_DISABLED
		;dz_more_slot_options_off()
		toggle_more_slot_options = False
EndFunction

Function dztoggle_npc_slots_on()
	npcchoice = OPTION_FLAG_NONE
		SetOptionFlagsST(npcchoice,False,"npc_slot31option")							;ungrey slot option
		SetOptionFlagsST(npcchoice,False,"npc_slot41option")							;ungrey slot option
		SetOptionFlagsST(npcchoice,False,"npc_slot42option")							;ungrey slot option
		optionsenable = OPTION_FLAG_DISABLED
		;dz_more_slot_options_off()
		toggle_more_slot_options = False
EndFunction

Function dztoggle_player_slots_off()
	;;this function is only called from the more_slot_options state on the even more options page;;;
	playerchoice = OPTION_FLAG_DISABLED
		ignorehair = False																;ensure variable is set to False
		toggle_hair = False
		ignorelonghair = False															;ensure variable is set to False
		toggle_longhair = False
		ignorecirclet = False															;ensure variable is set to False
		toggle_circlet = False
EndFunction

Function dztoggle_npc_slots_off()
	;;this function is also only called from the more_slot_options state on the even more options page;;;
	npcchoice = OPTION_FLAG_DISABLED
		ignorenpchair = False															;ensure variable is set to False
		toggle_npc_hair = False
		ignorenpclonghair = False														;ensure variable is set to False
		toggle_npc_longhair = False
		ignorenpccirclet = False														;ensure variable is set to False
		toggle_npc_circlet = False
EndFunction

Function dz_fill_npc_slots_used()
	;;;this code will produce two errors in papyrus.0.log for each teammate.GetEquippedArmorInSlot(i).GetName() that fails due to the slot being empty;;;
	;;;eg. Error: Cannot call GetName() on a None object, aborting function call stack:[dz_undress_MCM_menu (FE002D63)].dz_undress_mcm_menu_script.dz_fill_npc_slots_used() - "------------------------------" Line 2660;;;
	DEBUG_TRACE(NONE,"function dz_fill_npc_slots_used - teammate is "+teammate)
	If teammate
		teammate_name = teammate.GetBaseObject().GetName()
		ShowMessage("$npc_slots_message_text",False)
		Int i
		i = 30
		While i < 62
			If teammate.GetEquippedArmorInSlot(i).GetName()
			_npc_text_slots[i - 30] = teammate.GetEquippedArmorInSlot(i).GetName()
			Else
			_npc_text_slots[i - 30] = "$Empty"
			EndIf
			i = i +1
		EndWhile
	Else
		ShowMessage("$npc_slots_message_text2",False)
	EndIf
	ForcePageReset()
EndFunction

Function dz_empty_npc_slots_used()
	teammate_name = "$no_teammate"
	Int i
		i = 30
		While i < 62
			_npc_text_slots[i - 30] = ""
			SetTextOptionValueST(_npc_text_slots[i - 30])
			i = i +1
		EndWhile
	ForcePageReset()
EndFunction

;;;following functions adapted from Chesko: https://github.com/chesko256/CheskoPapyrusShared/blob/master/Scripts/Source/CommonArrayHelper.psc;;;
Bool Function ArrayAddString(String[] asArray, String asValue)
	;Adds a form to the first available non-None element in the array.
	;		False		=		Error (array full)
	;		True		=		Success
	Int i = 0
	While i < asArray.Length
		If asArray[i] == ""
			asArray[i] = asValue
			Return True
		Else
			i += 1
		EndIf
	EndWhile
	Return False
EndFunction

Bool Function ArrayRemoveString(String[] akArray, String akValue, Bool abSort = False)
    ;Removes a Message from the array, if found. Sorts the array using ArraySort() if bSort is True.
    ;       False       =       Error (string not found)
    ;       True        =       Success
    Int i = 0
    While i < akArray.Length
        If akArray[i] == akValue
            akArray[i] = ""
            If abSort == True
                ArraySortString(akArray)
            Endif
            Return True
        Else
            i += 1
        Endif
    EndWhile
    Return False
EndFunction

Bool Function ArraySortString(String[] akArray, Int i = 0)
	;Removes blank elements by shifting all elements down.
	;		   False		=			   No sorting required
	;		   True			=			   Success
	 Bool bFirstNoneFound = False
	 Int iFirstNonePos = i
	 While i < akArray.Length
		  If akArray[i] == ""
		  	   akArray[i] = ""
			   If bFirstNoneFound == False
					bFirstNoneFound = True
					iFirstNonePos = i
					i += 1
			   Else
					i += 1
			   EndIf
		  Else
			   If bFirstNoneFound == True
			   ;check to see if it's a couple of blank entries in a row
					If akArray[i] != ""
						 akArray[iFirstNonePos] = akArray[i]
						 akArray[i] = ""
						 ;Call this function recursively until it returns
						 ArraySortString(akArray, iFirstNonePos + 1)
						 Return True
					Else
						 i += 1
					EndIf
			   Else
					i += 1
			   EndIf
		  EndIf
	 EndWhile
	 Return False
EndFunction

String Function dz_array_item_names(Form[] array)                                       ;obtain names of array items
    String names
    Int j = 0
    While j < array.Length
        names = names + array[j].GetName()
        names = names + ", "
        j += 1
    EndWhile
    Return names as String
EndFunction
