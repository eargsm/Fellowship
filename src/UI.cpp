#include "UI.h"
#include "FriendlyFire.h"
#include "GoldShare.h"
#include "Homes.h"
#include "Party.h"
#include "Settings.h"

#include "SKSEMenuFramework.h"

namespace
{
	void HelpMarker(const char* a_text)
	{
		ImGuiMCP::SameLine();
		ImGuiMCP::TextDisabled("(?)");
		if (ImGuiMCP::IsItemHovered()) {
			ImGuiMCP::SetTooltip("%s", a_text);
		}
	}

	void SettingCheckbox(const char* a_label, std::atomic<bool>& a_value, const char* a_help)
	{
		bool v = a_value.load();
		if (ImGuiMCP::Checkbox(a_label, &v)) {
			a_value = v;
			Settings::Save();
			Party::SyncNow();
		}
		HelpMarker(a_help);
	}

	void SettingSliderInt(const char* a_label, std::atomic<int>& a_value, const char* a_help, int a_max = Settings::kMaxSlots,
		const char* a_format = "%d")
	{
		int v = a_value.load();
		ImGuiMCP::SetNextItemWidth(260.0f);
		if (ImGuiMCP::SliderInt(a_label, &v, 1, a_max, a_format, ImGuiMCP::ImGuiSliderFlags_AlwaysClamp)) {
			a_value = v;
		}
		if (ImGuiMCP::IsItemDeactivatedAfterEdit()) {
			Settings::Save();
			Party::SyncNow();
		}
		HelpMarker(a_help);
	}

	std::string MemberLine(const Party::Member& a_member)
	{
		auto line = a_member.name + (a_member.slot >= 0 ? std::format("  (slot {})", a_member.slot) : "");
		if (a_member.waiting) {
			line += a_member.waitDaysLeft >= 0.0f ? std::format("  - waiting ({:.1f} days left before they go home)", a_member.waitDaysLeft) : "  - waiting";
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
			ImGuiMCP::TextColored(ImGuiMCP::ImVec4{ 1.0f, 0.8f, 0.3f, 1.0f },
				"Over the limit: nobody is sent away, but you can't recruit another %s until you have fewer than %d.", a_noun, a_max);
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
		ImGuiMCP::Spacing();
		ImGuiMCP::Text("Homes: %d / %d", static_cast<int>(homes.size()), Homes::Capacity());
		for (const auto& home : homes) {
			ImGuiMCP::BulletText("%s%s%s", home.name.c_str(), home.withYou ? "  - with you, home waits" : "  - at home",
				home.loaded ? "" : " (not nearby)");
			ImGuiMCP::SameLine();
			const auto button = std::format("Forget##home{}", home.formID);
			if (ImGuiMCP::SmallButton(button.c_str())) {
				Homes::Forget(home.formID);
			}
		}
	}

	void Stat(const char* a_format, int a_value)
	{
		ImGuiMCP::Indent();
		ImGuiMCP::TextDisabled(a_format, a_value);
		ImGuiMCP::Unindent();
	}

	void __stdcall RenderExtra()
	{
		ImGuiMCP::SeparatorText("Extra");
		ImGuiMCP::BeginDisabled(!Settings::enabled.load());
		SettingCheckbox("No friendly fire", Settings::noFriendlyFire,
			"On: nobody in your party can hurt anyone - not you your followers, not them you or each other\n"
			"(weapons, arrows, hostile spells; heals still work) - and nobody turns on you however often you hit them.\n"
			"Follower-on-follower weapon hits are the one thing not blocked; a fight they start is calmed.\n"
			"Off (default): vanilla - hits land, and a follower you keep hitting fights back and leaves.");

		SettingCheckbox("Gold sharing", Settings::goldSharing,
			"On: your followers take a cut of everything you sell to merchants, so you are offered less.\n"
			"Followers with their own systems take one too while they are near you. Animals don't. Off (default): vanilla prices.");
		ImGuiMCP::BeginDisabled(!Settings::goldSharing.load());
		SettingSliderInt("Cut per follower", Settings::followerCut,
			"Each follower's share of a sale. All of them together never take more than half. Default 5%.",
			Settings::kMaxCutPerFollower, "%d%%");
		if (Settings::goldSharing.load()) {
			const auto followers = Party::GetStatus().goldFollowers;
			const auto total = (std::min)(followers * Settings::followerCut.load(), Settings::kMaxTotalCut);
			ImGuiMCP::Indent();
			ImGuiMCP::TextDisabled("With %d follower(s) now: they take %d%%, you keep %d%%", followers, total, 100 - total);
			ImGuiMCP::Unindent();
		}
		ImGuiMCP::EndDisabled();

		SettingCheckbox("Follower homes", Settings::followerHomes,
			"On: talk to a follower or animal companion you recruited the vanilla way and say \"This is your home now.\"\n"
			"Once dismissed, they walk there and live around it. \"Forget the home I gave you.\" undoes it. Followers with their own\n"
			"systems (Inigo, Lucien, ...) are left alone. Off (default): no home lines; homes you gave stay.\n"
			"First time after installing: save, then load that save, or the lines won't show yet.");

		SettingCheckbox("Sandbox when you stand still", Settings::sandboxWhenStill,
			"On: stop moving for a while and followers and animals you recruited the vanilla way relax around\n"
			"you. Move again and they follow. Not while you fight, sneak or ride. Off (default): they keep following.");
		ImGuiMCP::BeginDisabled(!Settings::sandboxWhenStill.load());
		SettingSliderInt("Seconds before they relax", Settings::sandboxDelay, "How long you stand still first. Default 30.",
			Settings::kMaxSandboxDelay);
		ImGuiMCP::EndDisabled();

		SettingCheckbox("Sandbox in inns and homes", Settings::sandboxInInns,
			"On: they relax the whole time you are inside an inn or a player home, and follow again when you leave.\n"
			"A modded place counts only if its location is marked as an inn or player home. Off (default): they keep following.");
		ImGuiMCP::EndDisabled();

		HomeList();
	}

	void RenderParty()
	{
		ImGuiMCP::SeparatorText("Party");
		const auto status = Party::GetStatus();
		if (!status.ready) {
			ImGuiMCP::TextColored(ImGuiMCP::ImVec4{ 1.0f, 0.5f, 0.4f, 1.0f }, "Fellowship.esp not found or no game loaded");
		} else {
			GroupList("Followers", "follower", status.followers, Settings::maxFollowers.load());
			ImGuiMCP::Spacing();
			GroupList("Animals", "animal", status.animals, Settings::maxAnimals.load());
		}

		ImGuiMCP::Spacing();
		if (ImGuiMCP::Button("Calm everyone down")) {
			Party::CalmEveryone();
		}
		HelpMarker("If a follower turned on you (or you on them) anyway: stops the fight between you and your party.\n"
				   "It also stops your own combat for a moment - enemies around will attack again.");
	}

	void __stdcall RenderSettings()
	{
		ImGuiMCP::SeparatorText("Fellowship");
		ImGuiMCP::TextWrapped("Travel with more than one follower. Recruit, dismiss and send them to wait with the normal vanilla dialogue.");
		ImGuiMCP::Spacing();

		SettingCheckbox("Mod enabled", Settings::enabled,
			"Off = back to the vanilla limit of one follower and one animal. Those already with you stay until you dismiss them.");

		ImGuiMCP::BeginDisabled(!Settings::enabled.load());
		SettingSliderInt("Max followers", Settings::maxFollowers,
			"How many followers recruited the vanilla way can travel with you. Followers with their own\n"
			"systems (Inigo, Lucien, ...) don't count and aren't limited. Default 5.");
		SettingSliderInt("Max animals", Settings::maxAnimals,
			"How many animal companions (dogs and other pets) can travel with you. A separate limit, as in\n"
			"vanilla: animals never take a follower's place. Default 5.");
		ImGuiMCP::EndDisabled();

		RenderParty();

		ImGuiMCP::Separator();
		if (ImGuiMCP::Button("Reset to defaults")) {
			Settings::ResetToDefaults();
			Party::SyncNow();
		}
		HelpMarker("Every setting on all three tabs back to its default.");
	}

	void __stdcall RenderDebug()
	{
		ImGuiMCP::SeparatorText("Debug");
		SettingCheckbox("Verbose log", Settings::verboseLog,
			"Extra detail in Documents/My Games/Skyrim Special Edition/SKSE/Fellowship.log.\n"
			"Turn it on, reproduce a problem, and post that log with your bug report.");
		ImGuiMCP::Spacing();
		ImGuiMCP::Text("This session:");
		Stat("Friendly spells blocked: %d", FriendlyFire::BlockedSpells());
		Stat("Friendly fights calmed: %d", FriendlyFire::CalmedFights());
		Stat("Gold your followers took: %d", GoldShare::TotalShared());
		const auto status = Party::GetStatus();
		if (!status.lastEvent.empty()) {
			ImGuiMCP::Spacing();
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
