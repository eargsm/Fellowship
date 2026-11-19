#include "UI.h"
#include "FriendlyFire.h"
#include "GoldShare.h"
#include "Homes.h"
#include "Party.h"
#include "Settings.h"

#include "SKSEMenuFramework.h"

namespace
{
	constexpr ImGuiMCP::ImVec4 kHelpColor{ 1.0f, 0.85f, 0.35f, 1.0f };
	constexpr ImGuiMCP::ImVec4 kWarnColor{ 1.0f, 0.5f, 0.4f, 1.0f };

	void Help(const char* a_text)
	{
		ImGuiMCP::Indent();
		ImGuiMCP::TextColored(kHelpColor, "%s", a_text);
		ImGuiMCP::Unindent();
	}

	template <class... Args>
	void Note(const char* a_format, Args... a_args)
	{
		ImGuiMCP::Indent();
		ImGuiMCP::TextDisabled(a_format, a_args...);
		ImGuiMCP::Unindent();
	}

	void Section(const char* a_title)
	{
		ImGuiMCP::Spacing();
		ImGuiMCP::SeparatorText(a_title);
	}

	void SettingCheckbox(const char* a_label, std::atomic<bool>& a_value, const char* a_help)
	{
		bool v = a_value.load();
		if (ImGuiMCP::Checkbox(a_label, &v)) {
			a_value = v;
			Settings::Save();
			Party::SyncNow();
		}
		Help(a_help);
	}

	void SettingSliderInt(const char* a_label, std::atomic<int>& a_value, const char* a_help, int a_max = Settings::kMaxSlots,
		const char* a_format = "%d", int a_min = 1)
	{
		int v = a_value.load();
		ImGuiMCP::SetNextItemWidth(260.0f);
		if (ImGuiMCP::SliderInt(a_label, &v, a_min, a_max, a_format, ImGuiMCP::ImGuiSliderFlags_AlwaysClamp)) {
			a_value = v;
		}
		if (ImGuiMCP::IsItemDeactivatedAfterEdit()) {
			Settings::Save();
			Party::SyncNow();
		}
		Help(a_help);
	}

	std::string MemberLine(const Party::Member& a_member)
	{
		auto line = a_member.name;
		if (a_member.waiting) {
			line += a_member.waitDaysLeft >= 0.0f ? std::format("  - waiting, {:.1f} days left", a_member.waitDaysLeft) : "  - waiting";
		}
		if (!a_member.loaded) {
			line += "  - not nearby";
		}
		return line;
	}

	void GroupList(const char* a_title, const char* a_noun, const Party::GroupStatus& a_group, int a_max)
	{
		ImGuiMCP::Text("%s: %d / %d", a_title, a_group.count, a_max);
		if (a_group.count > a_max) {
			ImGuiMCP::Indent();
			ImGuiMCP::TextColored(kWarnColor, "Over the limit: no new %s until you have fewer than %d.", a_noun, a_max);
			ImGuiMCP::Unindent();
		}
		for (const auto& member : a_group.members) {
			ImGuiMCP::BulletText("%s", MemberLine(member).c_str());
		}
	}

	void HomeList()
	{
		const auto homes = Homes::GetHomes();
		if (homes.empty()) {
			return;
		}
		ImGuiMCP::Text("Homes: %d / %d", static_cast<int>(homes.size()), Homes::Capacity());
		for (const auto& home : homes) {
			ImGuiMCP::BulletText("%s%s%s", home.name.c_str(), home.withYou ? "  - with you" : "  - at home",
				home.loaded ? "" : ", not nearby");
			ImGuiMCP::SameLine();
			const auto button = std::format("Forget##home{}", home.formID);
			if (ImGuiMCP::SmallButton(button.c_str())) {
				Homes::Forget(home.formID);
			}
		}
	}

	void __stdcall RenderSettings()
	{
		Section("General");
		SettingCheckbox("Mod enabled", Settings::enabled, "Off: back to vanilla, one follower and one animal.");

		ImGuiMCP::BeginDisabled(!Settings::enabled.load());
		SettingSliderInt("Max followers", Settings::maxFollowers,
			"Followers with their own systems (Inigo, Lucien, ...) don't count.");
		SettingSliderInt("Max animals", Settings::maxAnimals, "Dogs and other pets. They never take a follower's place.");
		ImGuiMCP::EndDisabled();

		Section("Party");
		const auto status = Party::GetStatus();
		if (!status.ready) {
			ImGuiMCP::TextColored(kWarnColor, "Fellowship.esp not found or no game loaded.");
		} else {
			GroupList("Followers", "followers", status.followers, Settings::maxFollowers.load());
			GroupList("Animals", "animals", status.animals, Settings::maxAnimals.load());
		}
		if (ImGuiMCP::Button("Calm everyone down")) {
			Party::CalmEveryone();
		}
		Help("Stops a fight between you and your party. Enemies nearby attack again.");

		Section("Reset");
		if (ImGuiMCP::Button("Reset to defaults")) {
			Settings::ResetToDefaults();
			Party::SyncNow();
		}
		Help("Every setting on all three pages.");
	}

	void __stdcall RenderExtra()
	{
		ImGuiMCP::BeginDisabled(!Settings::enabled.load());

		Section("Combat");
		SettingCheckbox("No friendly fire", Settings::noFriendlyFire,
			"Your party can't hurt you or each other, and nobody turns on you. Heals still work.");

		Section("Gold");
		SettingCheckbox("Gold sharing", Settings::goldSharing, "Followers take a cut of what you sell. Animals don't.");
		ImGuiMCP::BeginDisabled(!Settings::goldSharing.load());
		SettingSliderInt("Cut per follower", Settings::followerCut, "All of them together never take more than half.",
			Settings::kMaxCutPerFollower, "%d%%");
		if (Settings::goldSharing.load()) {
			const auto followers = Party::GetStatus().goldFollowers;
			const auto total = (std::min)(followers * Settings::followerCut.load(), Settings::kMaxTotalCut);
			Note("Now %d follower(s) take %d%%, you keep %d%%.", followers, total, 100 - total);
		}
		ImGuiMCP::EndDisabled();

		Section("Homes");
		SettingCheckbox("Follower homes", Settings::followerHomes,
			"Say \"This is your home now.\" Once dismissed, they live there. New install: save and reload first.");
		HomeList();

		Section("Relaxing");
		SettingCheckbox("When you stand still", Settings::sandboxWhenStill,
			"They relax around you. Not while you fight, sneak or ride.");
		ImGuiMCP::BeginDisabled(!Settings::sandboxWhenStill.load());
		SettingSliderInt("Seconds before they relax", Settings::sandboxDelay, "How long you stand still first.",
			Settings::kMaxSandboxDelay);
		ImGuiMCP::EndDisabled();
		SettingCheckbox("In inns and player homes", Settings::sandboxInInns, "They relax while you are inside.");
		SettingCheckbox("While waiting", Settings::sandboxWhileWaiting,
			"Told to wait, they sit, eat and wander within about 7 m instead of standing still.");

		Section("Waiting");
		SettingSliderInt("Days they wait", Settings::waitDays, "After you leave, before they go home. 0 = until you come back.",
			Settings::kMaxWaitDays, Settings::waitDays.load() == 0 ? "Forever" : "%d days", 0);

		ImGuiMCP::EndDisabled();
	}

	void __stdcall RenderDebug()
	{
		Section("Log");
		SettingCheckbox("Verbose log", Settings::verboseLog, "Extra detail in Fellowship.log. Post it with your bug report.");

		Section("This session");
		Note("Friendly spells blocked: %d", FriendlyFire::BlockedSpells());
		Note("Friendly fights calmed: %d", FriendlyFire::CalmedFights());
		Note("Gold your followers took: %d", GoldShare::TotalShared());
		const auto status = Party::GetStatus();
		if (!status.lastEvent.empty()) {
			ImGuiMCP::TextWrapped("Last event: %s", status.lastEvent.c_str());
		}
	}
}

void UI::Register()
{
	if (!SKSEMenuFramework::IsInstalled()) {
		logs::info("SKSE Menu Framework not installed - settings are INI-only");
		return;
	}
	SKSEMenuFramework::SetSection("Fellowship");
	SKSEMenuFramework::AddSectionItem("Settings", RenderSettings);
	SKSEMenuFramework::AddSectionItem("Extra", RenderExtra);
	SKSEMenuFramework::AddSectionItem("Debug", RenderDebug);
	logs::info("Registered SKSE Menu Framework pages");
}
