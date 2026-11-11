#include "Sandbox.h"
#include "Common.h"
#include "Settings.h"

namespace
{
	constexpr RE::FormID kSandboxNowLocal = 0x8A8;
	constexpr RE::FormID kLocTypeInn = 0x0001CB87;
	constexpr RE::FormID kLocTypePlayerHouse = 0x000FC1A3;
	constexpr float      kMoveDistance = 64.0f;

	using Clock = std::chrono::steady_clock;

	RE::TESGlobal*  g_sandboxNow{ nullptr };
	RE::BGSKeyword* g_inn{ nullptr };
	RE::BGSKeyword* g_playerHouse{ nullptr };

	Clock::time_point g_lastSync{ Clock::now() };
	RE::NiPoint3      g_stoppedAt;
	float             g_stillSeconds{ 0.0f };

	void UpdateStillTime(RE::PlayerCharacter* a_player)
	{
		const auto now = Clock::now();
		const auto elapsed = std::chrono::duration<float>(now - g_lastSync).count();
		g_lastSync = now;

		const auto ui = RE::UI::GetSingleton();
		const auto position = a_player->GetPosition();
		if (position.GetDistance(g_stoppedAt) > kMoveDistance || !ui || ui->IsMenuOpen(RE::DialogueMenu::MENU_NAME)) {
			g_stoppedAt = position;
			g_stillSeconds = 0.0f;
		} else if (!ui->GameIsPaused()) {
			g_stillSeconds += elapsed;
		}
	}

	bool InInnOrPlayerHome(RE::PlayerCharacter* a_player)
	{
		for (auto location = a_player->GetCurrentLocation(); location; location = location->parentLoc) {
			if ((g_inn && location->HasKeyword(g_inn)) || (g_playerHouse && location->HasKeyword(g_playerHouse))) {
				return true;
			}
		}
		return false;
	}

	bool Wanted(RE::PlayerCharacter* a_player)
	{
		if (!Settings::enabled || a_player->IsInCombat() || a_player->IsSneaking() || a_player->IsOnMount()) {
			return false;
		}
		const bool still = Settings::sandboxWhenStill && g_stillSeconds >= static_cast<float>(Settings::sandboxDelay.load());
		return still || (Settings::sandboxInInns && InInnOrPlayerHome(a_player));
	}
}

void Sandbox::OnDataLoaded()
{
	if (auto data = RE::TESDataHandler::GetSingleton()) {
		g_sandboxNow = data->LookupForm<RE::TESGlobal>(kSandboxNowLocal, Common::kPlugin);
	}
	g_inn = RE::TESForm::LookupByID<RE::BGSKeyword>(kLocTypeInn);
	g_playerHouse = RE::TESForm::LookupByID<RE::BGSKeyword>(kLocTypePlayerHouse);
	if (!g_sandboxNow) {
		logs::error("Sandbox global missing - is {} up to date? Sandboxing is off.", Common::kPlugin);
	}
}

void Sandbox::OnGameLoaded()
{
	g_lastSync = Clock::now();
	g_stillSeconds = 0.0f;
}

bool Sandbox::Sync()
{
	const auto player = RE::PlayerCharacter::GetSingleton();
	if (!g_sandboxNow || !player) {
		return false;
	}
	UpdateStillTime(player);
	const float wanted = Wanted(player) ? 1.0f : 0.0f;
	if (g_sandboxNow->value == wanted) {
		return false;
	}
	g_sandboxNow->value = wanted;
	if (Settings::verboseLog) {
		logs::info("Sandbox {}", wanted != 0.0f ? "on" : "off");
	}
	return true;
}
