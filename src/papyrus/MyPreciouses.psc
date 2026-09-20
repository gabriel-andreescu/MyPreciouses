Scriptname MyPreciouses Hidden

; Remember the actor's extra rings, including fingers and custom enchantments.
; One set is stored per actor. Calling again replaces it. The set survives saving/loading.
Function SaveExtraRings(Actor target) Global Native

; Restore available rings into empty fingers and consume the saved set.
; Current selections take priority. Native equipment is managed by the caller.
Function RestoreExtraRings(Actor target) Global Native
