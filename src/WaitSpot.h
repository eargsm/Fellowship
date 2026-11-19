#pragma once

namespace WaitSpot
{
	void OnDataLoaded(RE::TESQuest* a_partyQuest);
	void Update(int a_slot, RE::Actor* a_member, bool a_waiting);
	void Release(RE::Actor* a_member);
}
