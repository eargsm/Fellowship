#include "Party.h"
#include "Common.h"
#include "FriendlyFire.h"
#include "GoldShare.h"
#include "Homes.h"
#include "Sandbox.h"
#include "Settings.h"

namespace
{
	using Common::Name;

	constexpr RE::FormID kDialogueFollowerQuest = 0x000750BA;
	constexpr RE::FormID kFollowerHuntingBow = 0x0010E2DD;
	constexpr RE::FormID kFollowerIronArrow = 0x0010E2DE;
	constexpr RE::FormID kActorTypeNPC = 0x00013794;
	constexpr RE::FormID kPartyQuestLocal = 0x800;
	constexpr RE::FormID kSlotFactionLocal = 0x862;

	constexpr float kWaitDays = 3.0f;

	struct Group
	{
		const char*                  noun;
		std::uint32_t                vanillaAlias;
		int                          slotBase;
		RE::FormID                   globalID;
		std::atomic<int>&            max;
		bool                         npc;
		std::vector<RE::FormID>      factionIDs;
		RE::TESGlobal*               global{ nullptr };
		std::vector<RE::TESFaction*> factions;
	};

	Group g_followers{ "follower", 0, 0, 0x000BCC98, Settings::maxFollowers, true, { 0x0005C84E, 0x00084D1B, 0x000750B8 } };
	Group g_animals{ "animal", 1, Settings::kMaxSlots, 0x0005C84A, Settings::maxAnimals, false, { 0x0005C84E, 0x00084D1B } };
	std::array<Group*, 2> g_groups{ &g_followers, &g_animals };

	RE::TESQuest*       g_vanillaQuest{ nullptr };
	RE::TESQuest*       g_partyQuest{ nullptr };
	RE::TESBoundObject* g_huntingBow{ nullptr };
	RE::TESBoundObject* g_ironArrow{ nullptr };
	RE::BGSKeyword*     g_actorTypeNPC{ nullptr };
	RE::TESFaction*     g_slotFaction{ nullptr };

	std::mutex    g_statusLock;
	Party::Status g_status;

	std::unordered_set<int>               g_pendingClear;
	std::unordered_set<RE::FormID>        g_dismissing;
	std::unordered_map<RE::FormID, float> g_waitStart;

	void SetLastEvent(std::string a_text)
	{
		logs::info("{}", a_text);
		std::scoped_lock lock{ g_statusLock };
		g_status.lastEvent = std::move(a_text);
	}

	int SlotEnd(const Group& a_group) { return a_group.slotBase + Settings::kMaxSlots; }

	RE::Actor* SlotActor(int a_slot) { return Common::AliasActor(g_partyQuest, static_cast<std::uint32_t>(a_slot)); }
	RE::Actor* VanillaActor(const Group& a_group) { return Common::AliasActor(g_vanillaQuest, a_group.vanillaAlias); }

	int FindSlot(const Group& a_group, RE::Actor* a_actor)
	{
		for (int i = a_group.slotBase; i < SlotEnd(a_group); ++i) {
			if (!g_pendingClear.contains(i) && SlotActor(i) == a_actor) {
				return i;
			}
		}
		return -1;
	}

	Group* GroupOfSlotted(RE::Actor* a_actor)
	{
		for (auto group : g_groups) {
			if (FindSlot(*group, a_actor) >= 0) {
				return group;
			}
		}
		return nullptr;
	}

	bool IsTeammate(RE::Actor* a_actor)
	{
		return a_actor && !a_actor->IsPlayerRef() && !a_actor->IsDead() && a_actor->IsPlayerTeammate() && !a_actor->IsCommandedActor();
	}

	bool IsNPC(RE::Actor* a_actor) { return !g_actorTypeNPC || a_actor->HasKeyword(g_actorTypeNPC); }

	bool IsCompanion(const Group& a_group, RE::Actor* a_actor) { return IsTeammate(a_actor) && IsNPC(a_actor) == a_group.npc; }

	void ClearSlot(int a_slot, const char* a_why)
	{
		if (g_pendingClear.contains(a_slot)) {
			return;
		}
		SetLastEvent(std::format("{} leaves slot {} ({})", Name(SlotActor(a_slot)), a_slot, a_why));
		g_pendingClear.insert(a_slot);
		const auto args = RE::MakeFunctionArguments(static_cast<RE::TESQuest*>(g_partyQuest), std::int32_t{ a_slot });
		if (!Common::CallScript("ClearAlias", args, [a_slot]() { g_pendingClear.erase(a_slot); })) {
			g_pendingClear.erase(a_slot);
		}
	}

	void DismissMember(const Group& a_group, RE::Actor* a_actor, std::int32_t a_message, std::int32_t a_sayLine, std::string_view a_why)
	{
		const auto id = a_actor->GetFormID();
		if (!g_dismissing.insert(id).second) {
			return;
		}
		g_vanillaQuest->ForceRefIntoAlias(a_group.vanillaAlias, a_actor);
		SetLastEvent(std::format("{} {} and leaves the party", Name(a_actor), a_why));

		const auto onDone = [id]() { g_dismissing.erase(id); };
		bool sent;
		if (a_group.npc) {
			const auto args = RE::MakeFunctionArguments(static_cast<RE::TESQuest*>(g_vanillaQuest), std::int32_t{ a_message }, std::int32_t{ a_sayLine });
			sent = Common::CallScript("DismissPartyFollower", args, onDone);
		} else {
			const auto args = RE::MakeFunctionArguments(static_cast<RE::TESQuest*>(g_vanillaQuest));
			sent = Common::CallScript("DismissPartyAnimal", args, onDone);
		}
		if (!sent) {
			g_dismissing.erase(id);
		}
	}

	float DaysPassed()
	{
		const auto calendar = RE::Calendar::GetSingleton();
		return calendar ? calendar->GetDaysPassed() : 0.0f;
	}

	bool IsWaiting(RE::Actor* a_actor)
	{
		return a_actor->AsActorValueOwner()->GetActorValue(RE::ActorValue::kWaitingForPlayer) != 0.0f;
	}

	bool TickWaitTimer(RE::Actor* a_actor)
	{
		const auto id = a_actor->GetFormID();
		if (!IsWaiting(a_actor)) {
			if (g_waitStart.erase(id) && Settings::verboseLog) {
				logs::info("{} follows again - wait timer cancelled", Name(a_actor));
			}
			return false;
		}
		const auto it = g_waitStart.find(id);
		if (it != g_waitStart.end()) {
			return DaysPassed() - it->second >= kWaitDays;
		}
		if (!a_actor->Is3DLoaded()) {
			g_waitStart.emplace(id, DaysPassed());
			logs::info("{} is waiting and you left: they go home in {:.0f} days unless you come back for them", Name(a_actor), kWaitDays);
		}
		return false;
	}

	float WaitDaysLeft(RE::Actor* a_actor)
	{
		const auto it = g_waitStart.find(a_actor->GetFormID());
		return it == g_waitStart.end() ? -1.0f : (std::max)(0.0f, kWaitDays - (DaysPassed() - it->second));
	}

	constexpr std::uint32_t kCosaveID = 'FLWS';
	constexpr std::uint32_t kWaitRecord = 'WAIT';
	constexpr std::uint32_t kWaitVersion = 1;

	void OnSave(SKSE::SerializationInterface* a_intfc)
	{
		if (!a_intfc->OpenRecord(kWaitRecord, kWaitVersion)) {
			logs::error("Could not write the wait timers to the co-save");
			return;
		}
		const auto count = static_cast<std::uint32_t>(g_waitStart.size());
		a_intfc->WriteRecordData(count);
		for (const auto& [id, start] : g_waitStart) {
			a_intfc->WriteRecordData(id);
			a_intfc->WriteRecordData(start);
		}
	}

	void OnLoad(SKSE::SerializationInterface* a_intfc)
	{
		g_waitStart.clear();
		std::uint32_t type, version, length;
		while (a_intfc->GetNextRecordInfo(type, version, length)) {
			if (type != kWaitRecord || version != kWaitVersion) {
				continue;
			}
			std::uint32_t count = 0;
			a_intfc->ReadRecordData(count);
			for (std::uint32_t i = 0; i < count; ++i) {
				RE::FormID id = 0;
				float      start = 0.0f;
				if (!a_intfc->ReadRecordData(id) || !a_intfc->ReadRecordData(start)) {
					break;
				}
				if (RE::FormID resolved = 0; a_intfc->ResolveFormID(id, resolved)) {
					g_waitStart[resolved] = start;
				}
			}
		}
		logs::info("Loaded {} wait timer(s) from the co-save", g_waitStart.size());
	}

	void OnRevert(SKSE::SerializationInterface*) { g_waitStart.clear(); }

	void KeepFactions(const Group& a_group, RE::Actor* a_actor)
	{
		for (auto faction : a_group.factions) {
			if (faction && a_actor->GetFactionRank(faction, false) < 0) {
				a_actor->AddToFaction(faction, 0);
				if (Settings::verboseLog) {
					logs::info("Gave {} back faction {:08X}", Name(a_actor), faction->GetFormID());
				}
			}
		}
		if (g_slotFaction && a_actor->GetFactionRank(g_slotFaction, false) < 0) {
			a_actor->AddToFaction(g_slotFaction, 0);
		}
	}

	void LeaveSlot(const Group& a_group, RE::Actor* a_actor, int a_slot, RE::Actor* a_vanilla)
	{
		if (a_actor != a_vanilla) {
			for (auto faction : a_group.factions) {
				if (faction) {
					a_actor->RemoveFromFaction(faction);
				}
			}
		}
		if (g_slotFaction) {
			a_actor->RemoveFromFaction(g_slotFaction);
		}
		g_waitStart.erase(a_actor->GetFormID());
		ClearSlot(a_slot, a_actor->IsDead() ? "dead" : "no longer a teammate");
	}

	void UpdateMembers(const Group& a_group, RE::PlayerCharacter* a_player, RE::Actor* a_vanilla)
	{
		for (int i = a_group.slotBase; i < SlotEnd(a_group); ++i) {
			const auto actor = SlotActor(i);
			if (!actor || g_pendingClear.contains(i)) {
				continue;
			}
			if (actor->IsDead() || !actor->IsPlayerTeammate()) {
				LeaveSlot(a_group, actor, i, a_vanilla);
				continue;
			}
			if (g_dismissing.contains(actor->GetFormID())) {
				continue;
			}
			const bool turnedOnYou = a_group.npc && Settings::enabled && !Settings::noFriendlyFire && actor->IsInCombat() && actor->IsHostileToActor(a_player);
			if (turnedOnYou) {
				DismissMember(a_group, actor, 0, 0, "turned on you");
			} else if (TickWaitTimer(actor)) {
				g_waitStart.erase(actor->GetFormID());
				DismissMember(a_group, actor, 5, 1, "waited 3 days for you");
			} else {
				KeepFactions(a_group, actor);
			}
		}
	}

	void SlotNewRecruit(const Group& a_group, RE::Actor* a_vanilla)
	{
		if (!Settings::enabled || !IsCompanion(a_group, a_vanilla) || FindSlot(a_group, a_vanilla) >= 0) {
			return;
		}
		for (int i = a_group.slotBase; i < SlotEnd(a_group); ++i) {
			if (!g_pendingClear.contains(i) && !SlotActor(i)) {
				g_partyQuest->ForceRefIntoAlias(static_cast<std::uint32_t>(i), a_vanilla);
				SetLastEvent(std::format("{} joined (slot {})", Name(a_vanilla), i));
				return;
			}
		}
	}

	Party::GroupStatus ListMembers(const Group& a_group, RE::Actor* a_vanilla)
	{
		Party::GroupStatus             status;
		std::unordered_set<RE::FormID> seen;
		const auto add = [&](RE::Actor* a_actor, int a_slot) {
			if (IsCompanion(a_group, a_actor) && seen.insert(a_actor->GetFormID()).second) {
				status.members.push_back({ Name(a_actor), a_actor->GetFormID(), a_slot, IsWaiting(a_actor), a_actor->Is3DLoaded(),
					a_slot >= 0 ? WaitDaysLeft(a_actor) : -1.0f });
			}
		};
		for (int i = a_group.slotBase; i < SlotEnd(a_group); ++i) {
			if (!g_pendingClear.contains(i)) {
				add(SlotActor(i), i);
			}
		}
		add(a_vanilla, -1);
		status.count = static_cast<int>(status.members.size());
		return status;
	}

	void UpdateRecruitGlobal(const Group& a_group, int a_count)
	{
		const bool  full = Settings::enabled ? a_count >= a_group.max : a_count > 0;
		const float wanted = full ? 1.0f : 0.0f;
		if (a_group.global->value == wanted) {
			return;
		}
		if (Settings::verboseLog) {
			logs::info("{} count global {} -> {} ({} with you, max {})", a_group.noun, a_group.global->value, wanted, a_count, a_group.max.load());
		}
		a_group.global->value = wanted;
	}

	Party::GroupStatus SyncGroup(const Group& a_group, RE::PlayerCharacter* a_player)
	{
		const auto vanilla = VanillaActor(a_group);
		UpdateMembers(a_group, a_player, vanilla);
		SlotNewRecruit(a_group, vanilla);
		auto status = ListMembers(a_group, vanilla);
		UpdateRecruitGlobal(a_group, status.count);
		return status;
	}

	int CountGoldFollowers(const Party::GroupStatus& a_followers)
	{
		int count = a_followers.count;
		const auto lists = RE::ProcessLists::GetSingleton();
		if (!lists || !Settings::goldSharing) {
			return count;
		}
		const auto listed = [&](RE::Actor* a_actor) {
			return std::ranges::any_of(a_followers.members, [&](const auto& a_member) { return a_member.formID == a_actor->GetFormID(); });
		};
		for (auto& handle : lists->highActorHandles) {
			const auto actor = handle.get();
			if (IsCompanion(g_followers, actor.get()) && !listed(actor.get())) {
				++count;
			}
		}
		return count;
	}

	void EvaluateSlotMembers()
	{
		for (const auto group : g_groups) {
			for (int i = group->slotBase; i < SlotEnd(*group); ++i) {
				if (const auto actor = g_pendingClear.contains(i) ? nullptr : SlotActor(i)) {
					actor->EvaluatePackage();
				}
			}
		}
	}

	void Sync()
	{
		if (!g_partyQuest) {
			return;
		}
		const auto player = RE::PlayerCharacter::GetSingleton();
		if (!player || !player->Is3DLoaded()) {
			return;
		}
		FriendlyFire::Sync();
		auto followers = SyncGroup(g_followers, player);
		auto animals = SyncGroup(g_animals, player);
		const auto goldFollowers = CountGoldFollowers(followers);
		GoldShare::Sync(static_cast<std::size_t>(goldFollowers));
		Homes::Sync();
		if (Sandbox::Sync()) {
			EvaluateSlotMembers();
		}

		std::scoped_lock lock{ g_statusLock };
		g_status.ready = true;
		g_status.followers = std::move(followers);
		g_status.animals = std::move(animals);
		g_status.goldFollowers = goldFollowers;
	}

	std::int32_t CountItem(RE::Actor* a_actor, RE::TESBoundObject* a_item)
	{
		if (!a_item) {
			return 0;
		}
		const auto counts = a_actor->GetInventoryCounts([a_item](RE::TESBoundObject& a_obj) { return &a_obj == a_item; });
		const auto it = counts.find(a_item);
		return it != counts.end() ? it->second : 0;
	}

	void TakeBackHuntingBow(RE::Actor* a_actor, std::int32_t a_bowsBefore, std::int32_t a_arrowsBefore)
	{
		Common::RunLater(std::chrono::milliseconds(500), [handle = a_actor->CreateRefHandle(), a_bowsBefore, a_arrowsBefore]() {
			const auto actor = Common::HandleActor(handle);
			if (!actor) {
				return;
			}
			const auto extraBows = CountItem(actor, g_huntingBow) - a_bowsBefore;
			const auto extraArrows = CountItem(actor, g_ironArrow) - a_arrowsBefore;
			if (extraBows > 0) {
				actor->RemoveItem(g_huntingBow, extraBows, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
			}
			if (extraArrows > 0) {
				actor->RemoveItem(g_ironArrow, extraArrows, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
			}
			if (extraBows > 0 || extraArrows > 0) {
				logs::info("Swap items for {}: removed {} bow(s), {} arrow(s) the vanilla alias added", Name(actor), (std::max)(extraBows, 0),
					(std::max)(extraArrows, 0));
			}
		});
	}

	void OnDialogueOpened()
	{
		if (!g_partyQuest || !Settings::enabled) {
			return;
		}
		const auto topics = RE::MenuTopicManager::GetSingleton();
		const auto ref = topics ? topics->speaker.get() : nullptr;
		const auto speaker = ref ? ref->As<RE::Actor>() : nullptr;
		const auto group = speaker ? GroupOfSlotted(speaker) : nullptr;
		if (!group) {
			return;
		}
		const auto current = VanillaActor(*group);
		if (current == speaker) {
			return;
		}
		if (current && IsCompanion(*group, current) && FindSlot(*group, current) < 0) {
			SetLastEvent(std::format("Not swapping {} in: {} holds the vanilla {} alias without a slot", Name(speaker), Name(current), group->noun));
			return;
		}

		const auto bows = group->npc ? CountItem(speaker, g_huntingBow) : 0;
		const auto arrows = group->npc ? CountItem(speaker, g_ironArrow) : 0;
		g_vanillaQuest->ForceRefIntoAlias(group->vanillaAlias, speaker);
		SetLastEvent(std::format("Talking to {}: moved into the vanilla {} alias (was {})", Name(speaker), group->noun,
			current ? Name(current) : "empty"));
		if (group->npc) {
			TakeBackHuntingBow(speaker, bows, arrows);
		}
	}

	class MenuHandler : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
	{
	public:
		RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
		{
			if (a_event && a_event->menuName == RE::DialogueMenu::MENU_NAME) {
				if (a_event->opening) {
					SKSE::GetTaskInterface()->AddTask(OnDialogueOpened);
				} else {
					SKSE::GetTaskInterface()->AddTask(Sync);
				}
			}
			return RE::BSEventNotifyControl::kContinue;
		}
	};

	void StartTimer()
	{
		static std::atomic<bool> started{ false };
		if (started.exchange(true)) {
			return;
		}
		std::thread([]() {
			for (;;) {
				std::this_thread::sleep_for(std::chrono::milliseconds(500));
				SKSE::GetTaskInterface()->AddTask(Sync);
			}
		}).detach();
	}
}

void Party::OnDataLoaded()
{
	g_vanillaQuest = RE::TESForm::LookupByID<RE::TESQuest>(kDialogueFollowerQuest);
	g_huntingBow = RE::TESForm::LookupByID<RE::TESBoundObject>(kFollowerHuntingBow);
	g_ironArrow = RE::TESForm::LookupByID<RE::TESBoundObject>(kFollowerIronArrow);
	g_actorTypeNPC = RE::TESForm::LookupByID<RE::BGSKeyword>(kActorTypeNPC);
	for (auto group : g_groups) {
		group->global = RE::TESForm::LookupByID<RE::TESGlobal>(group->globalID);
		group->factions.clear();
		for (const auto id : group->factionIDs) {
			group->factions.push_back(RE::TESForm::LookupByID<RE::TESFaction>(id));
		}
	}
	if (auto data = RE::TESDataHandler::GetSingleton()) {
		g_partyQuest = data->LookupForm<RE::TESQuest>(kPartyQuestLocal, Common::kPlugin);
		g_slotFaction = data->LookupForm<RE::TESFaction>(kSlotFactionLocal, Common::kPlugin);
	}
	if (!g_partyQuest || !g_vanillaQuest || !g_followers.global || !g_animals.global) {
		logs::error("Missing forms (party quest {}, DialogueFollower {}, PlayerFollowerCount {}, PlayerAnimalCount {}) - is {} enabled? "
					"Fellowship is off.",
			g_partyQuest != nullptr, g_vanillaQuest != nullptr, g_followers.global != nullptr, g_animals.global != nullptr, Common::kPlugin);
		g_partyQuest = nullptr;
		return;
	}
	logs::info("Party quest {:08X} ({} slots: {} followers + {} animals), DialogueFollower {:08X}", g_partyQuest->GetFormID(),
		g_partyQuest->aliases.size(), Settings::kMaxSlots, Settings::kMaxSlots, g_vanillaQuest->GetFormID());

	static MenuHandler menuHandler;
	RE::UI::GetSingleton()->AddEventSink<RE::MenuOpenCloseEvent>(&menuHandler);
	StartTimer();
}

void Party::RegisterSerialization()
{
	const auto serialization = SKSE::GetSerializationInterface();
	serialization->SetUniqueID(kCosaveID);
	serialization->SetSaveCallback(OnSave);
	serialization->SetLoadCallback(OnLoad);
	serialization->SetRevertCallback(OnRevert);
}

Party::Status Party::GetStatus()
{
	std::scoped_lock lock{ g_statusLock };
	return g_status;
}

void Party::SyncNow()
{
	SKSE::GetTaskInterface()->AddTask(Sync);
}

void Party::CalmEveryone()
{
	SKSE::GetTaskInterface()->AddTask([]() {
		const auto player = RE::PlayerCharacter::GetSingleton();
		const auto lists = RE::ProcessLists::GetSingleton();
		if (!g_partyQuest || !player || !lists) {
			return;
		}
		int calmed = 0;
		for (auto& handle : lists->highActorHandles) {
			const auto actor = handle.get();
			if (IsTeammate(actor.get()) || (actor && GroupOfSlotted(actor.get()))) {
				Common::StopFighting(actor.get());
				++calmed;
			}
		}
		Common::StopFighting(player);
		SetLastEvent(std::format("Calmed {} companion(s) and you", calmed));
	});
}
