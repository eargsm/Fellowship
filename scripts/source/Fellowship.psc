Scriptname Fellowship Hidden

Function ClearAlias(Quest akQuest, int aiAliasID) global
	ReferenceAlias slot = akQuest.GetAlias(aiAliasID) as ReferenceAlias
	if slot
		slot.Clear()
	endif
EndFunction

Function DismissPartyFollower(Quest akDialogueFollower, int aiMessage, int aiSayLine) global
	DialogueFollowerScript dialogueFollower = akDialogueFollower as DialogueFollowerScript
	if dialogueFollower
		dialogueFollower.DismissFollower(aiMessage, aiSayLine)
	endif
EndFunction

Function DismissPartyAnimal(Quest akDialogueFollower) global
	DialogueFollowerScript dialogueFollower = akDialogueFollower as DialogueFollowerScript
	if dialogueFollower
		dialogueFollower.DismissAnimal()
	endif
EndFunction

Function SetFollowerHome(Quest akHomeQuest, Actor akFollower, int aiHome, int aiHomes, Faction akHasHome, Form akMarkerBase) global
	ReferenceAlias markerAlias = akHomeQuest.GetAlias(aiHome + aiHomes) as ReferenceAlias
	ObjectReference marker = markerAlias.GetReference()
	if marker
		marker.MoveTo(akFollower)
	else
		marker = akFollower.PlaceAtMe(akMarkerBase)
		markerAlias.ForceRefTo(marker)
	endif
	(akHomeQuest.GetAlias(aiHome) as ReferenceAlias).ForceRefTo(akFollower)
	akFollower.AddToFaction(akHasHome)
	akFollower.EvaluatePackage()
EndFunction

Function ForgetFollowerHome(Quest akHomeQuest, Actor akFollower, int aiHome, int aiHomes, Faction akHasHome) global
	ReferenceAlias markerAlias = akHomeQuest.GetAlias(aiHome + aiHomes) as ReferenceAlias
	ObjectReference marker = markerAlias.GetReference()
	(akHomeQuest.GetAlias(aiHome) as ReferenceAlias).Clear()
	markerAlias.Clear()
	if marker
		marker.Delete()
	endif
	if akFollower
		akFollower.RemoveFromFaction(akHasHome)
		akFollower.EvaluatePackage()
	endif
EndFunction
