#include "FriendlyFire.h"
#include "GoldShare.h"
#include "Homes.h"
#include "Party.h"
#include "Sandbox.h"
#include "Settings.h"
#include "UI.h"

SKSE_PLUGIN_LOAD(const SKSE::LoadInterface* a_skse)
{
	SKSE::Init(a_skse);

	logs::info("Fellowship loading");
	Settings::Load();
	FriendlyFire::Install();
	Party::RegisterSerialization();

	SKSE::GetMessagingInterface()->RegisterListener([](SKSE::MessagingInterface::Message* a_msg) {
		switch (a_msg->type) {
		case SKSE::MessagingInterface::kPostLoad:
			UI::Register();
			break;
		case SKSE::MessagingInterface::kDataLoaded:
			FriendlyFire::OnDataLoaded();
			GoldShare::OnDataLoaded();
			Homes::OnDataLoaded();
			Sandbox::OnDataLoaded();
			Party::OnDataLoaded();
			break;
		case SKSE::MessagingInterface::kPostLoadGame:
		case SKSE::MessagingInterface::kNewGame:
			FriendlyFire::OnGameLoaded();
			GoldShare::OnGameLoaded();
			Homes::OnGameLoaded();
			Sandbox::OnGameLoaded();
			Party::SyncNow();
			break;
		default:
			break;
		}
	});

	return true;
}
