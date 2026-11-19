#include "WaitSpot.h"
#include "Common.h"
#include "Settings.h"

namespace
{
	using Common::Name;

	constexpr RE::FormID    kWaitSpotFactionLocal = 0x8AA;
	constexpr RE::FormID    kXMarker = 0x0000003B;
	constexpr std::uint32_t kMarkerAliasBase = 2 * Settings::kMaxSlots;

	RE::TESQuest*   g_quest{ nullptr };
	RE::TESFaction* g_faction{ nullptr };
	RE::TESForm*    g_marker{ nullptr };

	std::unordered_set<int> g_pending;

	bool HasSpot(RE::Actor* a_member) { return a_member->GetFactionRank(g_faction, false) >= 0; }

	void PlaceSpot(int a_slot, RE::Actor* a_member)
	{
		const auto cell = a_member->GetParentCell();
		logs::info("{} waits and relaxes here ({})", Name(a_member), cell && cell->GetName() && *cell->GetName() ? cell->GetName() : "outside");
		g_pending.insert(a_slot);
		const auto args = RE::MakeFunctionArguments(static_cast<RE::TESQuest*>(g_quest), static_cast<RE::Actor*>(a_member),
			std::int32_t{ static_cast<std::int32_t>(kMarkerAliasBase) + a_slot }, static_cast<RE::TESFaction*>(g_faction),
			static_cast<RE::TESForm*>(g_marker));
		if (!Common::CallScript("SetWaitSpot", args, [a_slot]() { g_pending.erase(a_slot); })) {
			g_pending.erase(a_slot);
		}
	}
}

void WaitSpot::OnDataLoaded(RE::TESQuest* a_partyQuest)
{
	g_quest = a_partyQuest;
	if (auto data = RE::TESDataHandler::GetSingleton()) {
		g_faction = data->LookupForm<RE::TESFaction>(kWaitSpotFactionLocal, Common::kPlugin);
	}
	g_marker = RE::TESForm::LookupByID(kXMarker);
	if (!g_quest || !g_faction || !g_marker || g_quest->aliases.size() < kMarkerAliasBase + 2 * Settings::kMaxSlots) {
		logs::error("Wait spot forms missing - is {} up to date? Relaxing while waiting is off.", Common::kPlugin);
		g_quest = nullptr;
	}
}

void WaitSpot::Update(int a_slot, RE::Actor* a_member, bool a_waiting)
{
	if (!g_quest || g_pending.contains(a_slot)) {
		return;
	}
	const bool wanted = Settings::enabled && Settings::sandboxWhileWaiting && a_waiting;
	if (!wanted) {
		Release(a_member);
	} else if (!HasSpot(a_member) && a_member->Is3DLoaded()) {
		PlaceSpot(a_slot, a_member);
	}
}

void WaitSpot::Release(RE::Actor* a_member)
{
	if (!g_quest || !HasSpot(a_member)) {
		return;
	}
	a_member->RemoveFromFaction(g_faction);
	a_member->EvaluatePackage();
	logs::info("{} stops relaxing at their wait spot", Name(a_member));
}
