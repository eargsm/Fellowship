#include "GoldShare.h"
#include "Common.h"
#include "Settings.h"

namespace
{
	constexpr RE::FormID kGoldPerkLocal = 0x830;
	constexpr RE::FormID kGold = 0x0000000F;
	constexpr RE::FormID kPlayer = 0x00000014;

	RE::BGSPerk*                           g_perk{ nullptr };
	RE::BGSEntryPointFunctionDataOneValue* g_multiplier{ nullptr };

	std::optional<bool> g_perkApplied;
	double              g_barterShare{ 0.0 };

	std::atomic<int> g_totalShared{ 0 };

	void Notify(const std::string& a_text)
	{
		const auto vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
		RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback;
		if (!vm || !vm->DispatchStaticCall("Debug", "Notification", RE::MakeFunctionArguments(RE::BSFixedString{ a_text }), callback)) {
			logs::warn("Could not show the notification: {}", a_text);
		}
	}

	float WantedMultiplier(std::size_t a_followers)
	{
		if (!Settings::enabled || !Settings::goldSharing || a_followers == 0) {
			return 1.0f;
		}
		const auto cut = (std::min)(static_cast<int>(a_followers) * Settings::followerCut.load(), Settings::kMaxTotalCut);
		return 1.0f - static_cast<float>(cut) / 100.0f;
	}

	void OnSale(std::int32_t a_proceeds)
	{
		const auto multiplier = g_multiplier->data;
		if (multiplier >= 1.0f) {
			return;
		}
		const auto share = a_proceeds / static_cast<double>(multiplier) - a_proceeds;
		g_barterShare += share;
		if (Settings::verboseLog) {
			logs::info("Sale of {} gold at x{:.2f}: your followers took {:.1f}", a_proceeds, multiplier, share);
		}
	}

	void OnBarterClosed()
	{
		const auto shared = static_cast<int>(std::lround(g_barterShare));
		g_barterShare = 0.0;
		if (shared > 0) {
			g_totalShared += shared;
			const auto text = std::format("Your followers took {} gold as their share.", shared);
			Notify(text);
			logs::info("{}", text);
		}
	}

	class ContainerHandler : public RE::BSTEventSink<RE::TESContainerChangedEvent>
	{
	public:
		RE::BSEventNotifyControl ProcessEvent(const RE::TESContainerChangedEvent* a_event,
			RE::BSTEventSource<RE::TESContainerChangedEvent>*) override
		{
			if (a_event && a_event->newContainer == kPlayer && a_event->baseObj == kGold && a_event->itemCount > 0 &&
				a_event->oldContainer != kPlayer) {
				const auto ui = RE::UI::GetSingleton();
				if (ui && ui->IsMenuOpen(RE::BarterMenu::MENU_NAME)) {
					SKSE::GetTaskInterface()->AddTask([count = a_event->itemCount]() { OnSale(count); });
				}
			}
			return RE::BSEventNotifyControl::kContinue;
		}
	};

	class BarterHandler : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
	{
	public:
		RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
		{
			if (a_event && a_event->menuName == RE::BarterMenu::MENU_NAME && !a_event->opening) {
				SKSE::GetTaskInterface()->AddTask(OnBarterClosed);
			}
			return RE::BSEventNotifyControl::kContinue;
		}
	};
}

void GoldShare::OnDataLoaded()
{
	if (auto data = RE::TESDataHandler::GetSingleton()) {
		g_perk = data->LookupForm<RE::BGSPerk>(kGoldPerkLocal, Common::kPlugin);
	}
	if (g_perk) {
		for (const auto entry : g_perk->perkEntries) {
			const auto point = entry && entry->GetType() == RE::PERK_ENTRY_TYPE::kEntryPoint ?
			                       static_cast<RE::BGSEntryPointPerkEntry*>(entry) :
			                       nullptr;
			if (point && point->functionData && point->functionData->GetType() == RE::BGSEntryPointFunctionData::ENTRY_POINT_FUNCTION_DATA::kOneValue) {
				g_multiplier = static_cast<RE::BGSEntryPointFunctionDataOneValue*>(point->functionData);
			}
		}
	}
	if (!g_perk || !g_multiplier) {
		logs::error("Gold sharing forms missing (perk {}, entry {}) - is {} up to date? Gold sharing is off.", g_perk != nullptr,
			g_multiplier != nullptr, Common::kPlugin);
		g_perk = nullptr;
		return;
	}
	static ContainerHandler containerHandler;
	RE::ScriptEventSourceHolder::GetSingleton()->AddEventSink<RE::TESContainerChangedEvent>(&containerHandler);
	static BarterHandler barterHandler;
	RE::UI::GetSingleton()->AddEventSink<RE::MenuOpenCloseEvent>(&barterHandler);
	logs::info("Gold sharing perk {:08X} ready", g_perk->GetFormID());
}

void GoldShare::OnGameLoaded()
{
	g_perkApplied.reset();
	g_barterShare = 0.0;
}

void GoldShare::Sync(std::size_t a_followers)
{
	if (!g_perk) {
		return;
	}
	const auto multiplier = WantedMultiplier(a_followers);
	if (multiplier != g_multiplier->data) {
		if (Settings::verboseLog) {
			logs::info("Gold sharing: sell prices x{:.2f} ({} follower(s))", multiplier, a_followers);
		}
		g_multiplier->data = multiplier;
	}

	const auto player = RE::PlayerCharacter::GetSingleton();
	const bool want = multiplier < 1.0f;
	if (player && g_perkApplied != want) {
		g_perkApplied = want;
		Common::SetPerk(player, g_perk, want);
	}
}

int GoldShare::TotalShared() { return g_totalShared; }
