Scriptname	dz_undress_player_effect_script	Extends	ActiveMagicEffect
{;;;this script is for the magic effect that does the undressing of the player in the spell used by other scripts;;;}
Actor Property PlayerRef Auto

dz_undress_MCM_menu_script	Property	mcm_script	Auto

GlobalVariable Property dz_player_is_undressed	Auto									;0 = dressed, 1 = undressed , to track player undressed status in global variable

Actor	target
Int		iCameraState																	;this will store the view mode, 0 = first person
Bool 	Disrobed
Form[]  Items																			;this will store all equipped items except spells
Spell[] Spell_1																			;to store equipped spells
Armor[]	Slots																			;this array will hold equipped items

Weapon	Lefthand																		;we need to store the weapons in each hand separately
Weapon	Righthand
Magiceffect	Property	dz_undress_player_effect	Auto
Magiceffect	Property	dz_undress_player_anim_effect	Auto


;;;these properties are for the undress animation;;;
ImageSpaceModifier	Property	FadeToBlackIMod		Auto
ImageSpaceModifier	Property	FadeToBlackHoldImod	Auto
ImageSpaceModifier	Property	FadeToBlackBackIMod	Auto
Sound				Property	DRScBodyOpen		Auto
Bool	animated																		;vluse determined by which magiceffect this script is running on

;;;for debugging function;;;
Bool	dbg

Function DEBUG_TRACE(Actor akActor, String sMsg)
	If dbg
		If akActor
			debug.trace("DPHBU:"+self+": "+akActor.GetDisplayname()+": "+sMsg)
		Else
			debug.trace("DPHBU:"+self+": "+sMsg)
		EndIf
	EndIf
EndFunction

Event OnEffectStart(Actor akTarget, Actor akCaster)
	dbg = mcm_script.toggle_debug														;get value of debug toggle in MCM menu
	DEBUG_TRACE(akTarget,"Player undressing magiceffect started")
	target = akTarget
	GoToState("")
	dz_player_undresses()																;if so call the function to undress the player
EndEvent

Event OnBeginState()
	;;;is this script running on the animated version of the magiceffect?;;;
	Magiceffect effect = self.GetBaseObject()
	DEBUG_TRACE(None,"self is "+effect)
	dbg = mcm_script.toggle_debug
	DEBUG_TRACE(target As Actor,"event OnBeginState")
	If target == PlayerRef																;checks to see if the trigger is the player
		If effect == dz_undress_player_effect
			animated = False
		ElseIf effect == dz_undress_player_anim_effect
			animated = True
		Else
			DEBUG_TRACE(NONE,"something went wrong with detecting the magic effect version")
		EndIf
	EndIf
EndEvent

Event OnEffectFinish(Actor akTarget, Actor akCaster)
	dbg = mcm_script.toggle_debug
	dz_player_redresses()
	DEBUG_TRACE(akTarget,"Player undress magiceffect finished")
EndEvent

Function dz_player_undresses()															;define the function to undress the player
	DEBUG_TRACE(PlayerRef,"magiceffect has called dz_undress_player")
	If Disrobed == True
		Return
	EndIf
	iCameraState = Game.GetCameraState()												;store whether first/third person
	If iCameraState == 0 && mcm_script.third_person == True
		Game.ForceThirdPerson()
	EndIf
	;;;if the script is running on the animated magic effect then 'animated' should equal True;;;
	DEBUG_TRACE(PlayerRef,"animated is "+animated)
	dz_undress_anim(PlayerRef,animated)
	dz_fade_out(animated)

	Lefthand = PlayerRef.GetEquippedWeapon(True)										;store the left hand weapon - if any
	Righthand = PlayerRef.GetEquippedWeapon(False)										;store the righthand weapon - if any
	Items = PO3_SKSEFunctions.AddAllEquippedItemsToArray(PlayerRef)						;store all equipped items in an array using https://www.nexusmods.com/skyrimspecialedition/mods/22854
	Int i																				;define this variable for use in arrays
	Spell_1 = new Spell[4]																;create the spells array
	i = spell_1.Length																	;size of spells array (should be 4)
	While (i)
		i = i - 1
		spell_1[i] = PlayerRef.GetEquippedSpell(i)										;fill the spells array
	EndWhile
	Slots = New Armor[32]
	If mcm_script.player_hair_slots == True || mcm_script.npc_hair_slots == True || mcm_script.all_slots == True
		Slots = dz_get_all_player_slots()
		DEBUG_TRACE(PlayerRef,"issued dz_get_all_player_slots")
	EndIf
	If mcm_script.slow_unequip == True
		dz_slow_unequip(PlayerRef)
		DEBUG_TRACE(PlayerRef,"issued dz_slow_unequip")
	Else
		MyPreciouses.SaveExtraRings(PlayerRef)
		PlayerRef.UnEquipAll()															;all stored, strip the player
		DEBUG_TRACE(PlayerRef,"issued unequipall()")
	EndIf
	If mcm_script.player_hair_slots == True
		DEBUG_TRACE(PlayerRef,"player_hair_slots = True, re-equipping")
		If mcm_script.ignorehair == True												;re-equip problematic slots if enabled
			PlayerRef.equipitemex(slots[1],0,False,True)
		EndIf
		If mcm_script.ignorelonghair == True
			PlayerRef.equipitemex(slots[11],0,False,True)
		EndIf
		If mcm_script.ignorecirclet == True
			PlayerRef.equipitemex(slots[12],0,False,True)
		EndIf
	ElseIf mcm_script.all_slots == True
		dz_fill_player_slots()
		DEBUG_TRACE(PlayerRef,"issued dz_fill_player_slots")
	EndIf
	Disrobed = True																		;store the undressed state of the player
	dz_player_is_undressed.SetValue(1.0)
	dz_fade_in(animated)
	DEBUG_TRACE(PlayerRef,"dz_undress_player - dz_player_is_undressed is "+dz_player_is_undressed.GetValue())
EndFunction

Function dz_player_redresses()															;define the function to re-dress the player
	DEBUG_TRACE(PlayerRef,"magiceffect has called dz_redress_player")
	If mcm_script.third_person == True && iCameraState == 0								;if the MCM menu to force third person on entry is toggled on and the player was originally in first person
		Game.ForceFirstPerson()															;then force back to first person
	EndIf
	If Disrobed == False																;the player is not undressed
		Return																			;exit the function as re-dressing not needed
	EndIf
	;;;if the script is running on the animated magic effect then 'animated' should equal True;;;
	dz_fade_out(animated)

	PlayerRef.EquipItemEx(Righthand,1,False,True)										;re-equip the righthand
	PlayerRef.EquipItemEx(Lefthand,2,False,True)										;re-equip the lefthand
	Int i																				;define this variable for use in arrays
	i = 0
	While i < items.length																;loop through the stored items
		If Items[i] As Armor || Items[i] As Ammo ||  Items[i] As Light					;if stored items are armour or ammo or torch
			PlayerRef.EquipItemEx(Items[i],0,False,True)								;re-equip them
		EndIf
		i = i +1
	EndWhile
	i = spell_1.Length
	While (i)																			;loop through the stored spells
		i = i - 1
		spell sp = spell_1[i]															;if that spell position exists
		If ( sp )
			PlayerRef.EquipSpell(sp,i)													;fill it
		EndIf
	EndWhile
	MyPreciouses.RestoreExtraRings(PlayerRef)
	Disrobed = False																	;store the dressed state of the player
	dz_player_is_undressed.SetValue(0.0)
	dz_fade_in(animated)
	DEBUG_TRACE(PlayerRef,"dz_redress_player - dz_player_is_undressed is now "+dz_player_is_undressed.GetValue())
EndFunction

Armor[] Function dz_get_all_player_slots()
	Armor[] temp
	temp = New Armor[32]
	Int i = 0
	While i < temp.Length
		temp[i] = PlayerRef.GetEquippedArmorInSlot(i + 30)
		i += 1
	EndWhile
	Return temp
EndFunction

Function dz_fill_player_slots()
	Int i = 0
	While i < mcm_script.ignoremoreslots.Length
		If mcm_script.ignoremoreslots[i] == True
		PlayerRef.equipitemex(slots[i],0,False,True)
		EndIf
		i += 1
	EndWhile
EndFunction

Function dz_slow_unequip(Actor aRef)
	Form[] check_items = PO3_SKSEFunctions.AddAllEquippedItemsToArray(aRef)				;store all equipped items in an array
	DEBUG_TRACE(aRef,"dz_slow_unequip: check_items array is "+check_items)
	Int arr_length = check_items.Length
	Int i = 0
	String[] kwords = mcm_script.keywords_list
	DEBUG_TRACE(aRef,"checking keywords_list: kwords = "+kwords)
	Bool keep
	While i < arr_length
		Int j = 0
		keep = False
		While j < kwords.Length
			;/ Keyword testKW = Keyword.GetKeyword(kwords[j])
			If check_items[i].HasKeyword(testKW) == True
				DEBUG_TRACE(aRef,"keyword "+testKW+" found, setting 'keep' = True")
				keep = True
			EndIf /;
			If check_items[i].HasKeywordString(kwords[j]) == True
				DEBUG_TRACE(aRef,"keyword string "+kwords[j]+" found, setting 'keep' = True")
				keep = True
			EndIf
			j += 1
		EndWhile
		If keep == False
			aRef.UnEquipitemEx(check_items[i])
		EndIf
		i += 1
	EndWhile
EndFunction

Function dz_undress_anim(Actor akActor,Bool anim)
	If anim == False
		Return
	EndIf
	Game.DisablePlayerControls()
	Game.ForceThirdPerson()
	debug.SendAnimationEvent(akActor,"IdleMQ203EsbernBookEnterInstant")
	DRScBodyOpen.Play(akActor)
	Utility.Wait(1.2)
EndFunction

Function dz_fade_out(Bool anim)
	If anim == False
		Return
	EndIf
	FadeToBlackIMod.Apply()
	Utility.Wait(2)
	Game.FadeOutGame(False,True,50,1)
	FadeToBlackHoldImod.Apply()
EndFunction

Function dz_fade_in(Bool anim)
	If anim == False
		Return
	EndIf
	Utility.Wait(0.5)
	Game.FadeOutGame(False,True,0.1,0.1)
	FadeToBlackHoldIMod.PopTo(FadeToBlackBackIMod,1)
	Game.EnablePlayerControls()
EndFunction
