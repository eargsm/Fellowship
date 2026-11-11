#include "Homes.h"
#include "Common.h"
#include "Settings.h"

namespace
{
	using Common::Name;

	constexpr RE::FormID kHomeQuestLocal = 0x860;
	constexpr RE::FormID kHasHomeFactionLocal = 0x861;
	constexpr RE::FormID kHomesOnGlobalLocal = 0x863;
	constexpr RE::FormID kSetHomeInfoLocal = 0x866;
	constexpr RE::FormID kForgetHomeInfoLocal = 0x869;
	constexpr RE::FormID kAnimalSetHomeInfoLocal = 0x8A4;
	constexpr RE::FormID kAnimalForgetHomeInfoLocal = 0x8A7;
	constexpr int        kHomes = 50;
	constexpr RE::FormID kXMarker = 0x0000003B;

	RE::TESQuest*   g_quest{ nullptr };
	RE::TESFaction* g_hasHome{ nullptr };
	RE::TESGlobal*  g_homesOn{ nullptr };
	RE::TESForm*    g_marker{ nullptr };
	RE::FormID      g_setInfo{ 0 };
	RE::FormID      g_forgetInfo{ 0 };
	RE::FormID      g_animalSetInfo{ 0 };
	RE::FormID      g_animalForgetInfo{ 0 };

	std::unordered_set<int>  g_pending;
	std::mutex               g_lock;
	std::vector<Homes::Home> g_homes;

	RE::Actor* HomeActor(int a_home) { return Common::AliasActor(g_quest, static_cast<std::uint32_t>(a_home)); }

	int FindHome(RE::Actor* a_actor)
	{
		for (int i = 0; i < kHomes; ++i) {
			if (!g_pending.contains(i) && HomeActor(i) == a_actor) {
				return i;
			}
		}
		return -1;
	}

	int FindFreeHome()
	{
		for (int i = 0; i < kHomes; ++i) {
			if (!g_pending.contains(i) && !HomeActor(i)) {
				return i;
			}
		}
		return -1;
	}

	void CallForHome(int a_home, const char* a_function, RE::BSScript::IFunctionArguments* a_args)
	{
		g_pending.insert(a_home);
		if (!Common::CallScript(a_function, a_args, [a_home]() { g_pending.erase(a_home); })) {
			g_pending.erase(a_home);
		}
	}

	void SetHome(RE::Actor* a_follower)
	{
		const int  current = FindHome(a_follower);
		const bool moved = current >= 0;
		const int  home = moved ? current : FindFreeHome();
		if (home < 0) {
			logs::info("{} gets no home: all {} homes are taken", Name(a_follower), kHomes);
			return;
		}
		const auto cell = a_follower->GetParentCell();
		const auto place = cell && cell->GetName() && *cell->GetName() ? cell->GetName() : "here";
		logs::info("{} {} home {} ({})", Name(a_follower), moved ? "moves to" : "gets", home, place);
		CallForHome(home, "SetFollowerHome",
			RE::MakeFunctionArguments(static_cast<RE::TESQuest*>(g_quest), static_cast<RE::Actor*>(a_follower), std::int32_t{ home },
				std::int32_t{ kHomes }, static_cast<RE::TESFaction*>(g_hasHome), static_cast<RE::TESForm*>(g_marker)));
	}

	void ForgetHome(RE::Actor* a_follower, int a_home, std::string_view a_why)
	{
		logs::info("{} leaves home {} ({})", Name(a_follower), a_home, a_why);
		CallForHome(a_home, "ForgetFollowerHome",
			RE::MakeFunctionArguments(static_cast<RE::TESQuest*>(g_quest), static_cast<RE::Actor*>(a_follower), std::int32_t{ a_home },
				std::int32_t{ kHomes }, static_cast<RE::TESFaction*>(g_hasHome)));
	}

	class TopicHandler : public RE::BSTEventSink<RE::TESTopicInfoEvent>
	{
	public:
		RE::BSEventNotifyControl ProcessEvent(const RE::TESTopicInfoEvent* a_event, RE::BSTEventSource<RE::TESTopicInfoEvent>*) override
		{
			if (!a_event || a_event->type != RE::TESTopicInfoEvent::TopicInfoEventType::kTopicEnd || !a_event->speakerRef) {
				return RE::BSEventNotifyControl::kContinue;
			}
			const auto info = a_event->topicInfoFormID;
			const auto actor = a_event->speakerRef->As<RE::Actor>();
			const bool set = info == g_setInfo || info == g_animalSetInfo;
			const bool forget = info == g_forgetInfo || info == g_animalForgetInfo;
			if (actor && (set || forget)) {
				SKSE::GetTaskInterface()->AddTask([set, handle = actor->CreateRefHandle()]() {
					const auto follower = Common::HandleActor(handle);
					if (!follower || !g_quest) {
						return;
					}
					if (set) {
						SetHome(follower);
					} else if (const int home = FindHome(follower); home >= 0) {
						ForgetHome(follower, home, "you told them to forget it");
					}
				});
			}
			return RE::BSEventNotifyControl::kContinue;
		}
	};
}

void Homes::OnDataLoaded()
{
	if (auto data = RE::TESDataHandler::GetSingleton()) {
		g_quest = data->LookupForm<RE::TESQuest>(kHomeQuestLocal, Common::kPlugin);
		g_hasHome = data->LookupForm<RE::TESFaction>(kHasHomeFactionLocal, Common::kPlugin);
		g_homesOn = data->LookupForm<RE::TESGlobal>(kHomesOnGlobalLocal, Common::kPlugin);
	}
	g_marker = RE::TESForm::LookupByID(kXMarker);
	if (!g_quest || !g_hasHome || !g_homesOn || !g_marker) {
		logs::error("Follower home forms missing (quest {}, faction {}, global {}) - is {} up to date? Follower homes are off.",
			g_quest != nullptr, g_hasHome != nullptr, g_homesOn != nullptr, Common::kPlugin);
		g_quest = nullptr;
		return;
	}
	const auto questID = g_quest->GetFormID();
	const auto prefix = questID & ((questID >> 24) == 0xFE ? 0xFFFFF000u : 0xFF000000u);
	g_setInfo = prefix | kSetHomeInfoLocal;
	g_forgetInfo = prefix | kForgetHomeInfoLocal;
	g_animalSetInfo = prefix | kAnimalSetHomeInfoLocal;
	g_animalForgetInfo = prefix | kAnimalForgetHomeInfoLocal;

	static TopicHandler topicHandler;
	RE::ScriptEventSourceHolder::GetSingleton()->AddEventSink<RE::TESTopicInfoEvent>(&topicHandler);
	logs::info("Follower homes: quest {:08X} ({} homes), lines {:08X} / {:08X}, animals {:08X} / {:08X}", questID, kHomes, g_setInfo,
		g_forgetInfo, g_animalSetInfo, g_animalForgetInfo);
}

void Homes::OnGameLoaded()
{
	g_pending.clear();
}

void Homes::Sync()
{
	if (!g_quest || !g_quest->IsEnabled()) {
		return;
	}
	g_homesOn->value = Settings::enabled && Settings::followerHomes ? 1.0f : 0.0f;

	std::vector<Home> homes;
	for (int i = 0; i < kHomes; ++i) {
		const auto actor = g_pending.contains(i) ? nullptr : HomeActor(i);
		if (!actor) {
			continue;
		}
		if (actor->IsDead()) {
			ForgetHome(actor, i, "died");
			continue;
		}
		homes.push_back({ Name(actor), actor->GetFormID(), actor->IsPlayerTeammate(), actor->Is3DLoaded() });
	}
	std::scoped_lock lock{ g_lock };
	g_homes = std::move(homes);
}

std::vector<Homes::Home> Homes::GetHomes()
{
	std::scoped_lock lock{ g_lock };
	return g_homes;
}

void Homes::Forget(RE::FormID a_follower)
{
	SKSE::GetTaskInterface()->AddTask([a_follower]() {
		const auto actor = RE::TESForm::LookupByID<RE::Actor>(a_follower);
		if (const int home = actor && g_quest ? FindHome(actor) : -1; home >= 0) {
			ForgetHome(actor, home, "forgotten from the menu");
		}
	});
}

int Homes::Capacity() { return kHomes; }
