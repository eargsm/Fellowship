#include "FriendlyFire.h"
#include "Common.h"
#include "Settings.h"

namespace
{
	constexpr RE::FormID kFollowerPerkLocal = 0x801;
	constexpr RE::FormID kPlayerPerkLocal = 0x802;

	RE::BGSPerk*        g_followerPerk{ nullptr };
	RE::BGSPerk*        g_playerPerk{ nullptr };
	std::optional<bool> g_perksApplied;
	std::atomic<int>    g_blockedSpells{ 0 };
	std::atomic<int>    g_calmedFights{ 0 };

	constexpr std::array<const char*, 2> kAllyHitSettings{ "iAllyHitCombatAllowed", "iAllyHitNonCombatAllowed" };
	constexpr std::int32_t               kSparringTolerance = 100000;
	std::array<std::optional<std::int32_t>, kAllyHitSettings.size()> g_allyHitOriginal{};

	void ApplyAllyTolerance(bool a_sparring)
	{
		const auto settings = RE::GameSettingCollection::GetSingleton();
		for (std::size_t i = 0; settings && i < kAllyHitSettings.size(); ++i) {
			const auto setting = settings->GetSetting(kAllyHitSettings[i]);
			if (!setting || !g_allyHitOriginal[i]) {
				continue;
			}
			const auto wanted = a_sparring ? kSparringTolerance : *g_allyHitOriginal[i];
			if (setting->data.i != wanted) {
				setting->data.i = wanted;
				logs::info("{} = {}", kAllyHitSettings[i], wanted);
			}
		}
	}

	bool Active() { return Settings::enabled && Settings::noFriendlyFire; }

	bool InParty(RE::Actor* a_actor) { return a_actor && !a_actor->IsDead() && (a_actor->IsPlayerRef() || a_actor->IsPlayerTeammate()); }

	bool Protected(RE::Actor* a_attacker, RE::Actor* a_victim)
	{
		return Active() && a_attacker && a_victim && a_attacker != a_victim && InParty(a_attacker) && InParty(a_victim);
	}

	bool BlockFriendlySpell(RE::MagicTarget* a_target, const RE::MagicTarget::AddTargetData& a_data)
	{
		const auto effect = a_data.effect ? a_data.effect->baseEffect : nullptr;
		if (!Active() || !effect || !effect->IsHostile()) {
			return false;
		}
		const auto caster = a_data.caster ? a_data.caster->As<RE::Actor>() : nullptr;
		const auto statsRef = a_target->GetTargetStatsObject();
		const auto target = statsRef ? statsRef->As<RE::Actor>() : nullptr;
		if (!Protected(caster, target)) {
			return false;
		}
		++g_blockedSpells;
		if (Settings::verboseLog) {
			logs::info("Blocked friendly {} ({:08X}) from {} on {}", effect->GetName(), effect->GetFormID(), caster->GetDisplayFullName(),
				target->GetDisplayFullName());
		}
		return true;
	}

	template <std::size_t N>
	struct AddTargetHook
	{
		static bool thunk(RE::MagicTarget* a_this, RE::MagicTarget::AddTargetData& a_data)
		{
			return BlockFriendlySpell(a_this, a_data) ? false : func(a_this, a_data);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	void CalmIfHostile(RE::ObjectRefHandle a_attacker, RE::ObjectRefHandle a_victim)
	{
		const auto attacker = Common::HandleActor(a_attacker);
		const auto victim = Common::HandleActor(a_victim);
		if (!attacker || !victim) {
			return;
		}
		for (auto [actor, other] : { std::pair{ victim, attacker }, std::pair{ attacker, victim } }) {
			if (!actor->IsPlayerRef() && InParty(actor) && actor->IsHostileToActor(other)) {
				Common::StopFighting(actor);
				++g_calmedFights;
				logs::info("Friendly hit: calmed {} (turned on {})", actor->GetDisplayFullName(), other->GetDisplayFullName());
			}
		}
	}

	class HitHandler : public RE::BSTEventSink<RE::TESHitEvent>
	{
	public:
		RE::BSEventNotifyControl ProcessEvent(const RE::TESHitEvent* a_event, RE::BSTEventSource<RE::TESHitEvent>*) override
		{
			if (!a_event || !a_event->cause || !a_event->target) {
				return RE::BSEventNotifyControl::kContinue;
			}
			const auto attacker = a_event->cause->As<RE::Actor>();
			const auto victim = a_event->target->As<RE::Actor>();
			if (!Protected(attacker, victim)) {
				return RE::BSEventNotifyControl::kContinue;
			}
			const auto calm = [attackerHandle = attacker->CreateRefHandle(), victimHandle = victim->CreateRefHandle()]() {
				CalmIfHostile(attackerHandle, victimHandle);
			};
			SKSE::GetTaskInterface()->AddTask(calm);
			Common::RunLater(std::chrono::milliseconds(500), calm);
			return RE::BSEventNotifyControl::kContinue;
		}
	};
}

void FriendlyFire::Install()
{
	REL::Relocation<std::uintptr_t> player{ RE::VTABLE_PlayerCharacter[4] };
	AddTargetHook<0>::func = player.write_vfunc(0x1, AddTargetHook<0>::thunk);
	REL::Relocation<std::uintptr_t> npc{ RE::VTABLE_Character[4] };
	AddTargetHook<1>::func = npc.write_vfunc(0x1, AddTargetHook<1>::thunk);
	logs::info("Hooked MagicTarget::AddTarget (PlayerCharacter original {:X}, Character original {:X})",
		AddTargetHook<0>::func.address(), AddTargetHook<1>::func.address());
}

void FriendlyFire::OnDataLoaded()
{
	if (auto data = RE::TESDataHandler::GetSingleton()) {
		g_followerPerk = data->LookupForm<RE::BGSPerk>(kFollowerPerkLocal, Common::kPlugin);
		g_playerPerk = data->LookupForm<RE::BGSPerk>(kPlayerPerkLocal, Common::kPlugin);
	}
	if (!g_followerPerk || !g_playerPerk) {
		logs::error("No friendly fire perks missing from {} - weapon friendly fire stays on", Common::kPlugin);
	}
	if (const auto settings = RE::GameSettingCollection::GetSingleton()) {
		for (std::size_t i = 0; i < kAllyHitSettings.size(); ++i) {
			if (const auto setting = settings->GetSetting(kAllyHitSettings[i])) {
				g_allyHitOriginal[i] = setting->data.i;
				logs::info("{} original value {}", kAllyHitSettings[i], setting->data.i);
			} else {
				logs::warn("Game setting {} not found - followers may still turn on you after repeated hits", kAllyHitSettings[i]);
			}
		}
	}
	static HitHandler hitHandler;
	RE::ScriptEventSourceHolder::GetSingleton()->AddEventSink<RE::TESHitEvent>(&hitHandler);
}

void FriendlyFire::Sync()
{
	const bool want = Active();
	ApplyAllyTolerance(want);
	const auto player = RE::PlayerCharacter::GetSingleton();
	if (!player || g_perksApplied == want) {
		return;
	}
	g_perksApplied = want;
	for (const auto perk : { g_followerPerk, g_playerPerk }) {
		if (perk) {
			Common::SetPerk(player, perk, want);
		}
	}
}

void FriendlyFire::OnGameLoaded()
{
	g_perksApplied.reset();
}

int FriendlyFire::BlockedSpells() { return g_blockedSpells; }
int FriendlyFire::CalmedFights() { return g_calmedFights; }
