#include "Settings.h"

namespace
{
	const std::filesystem::path kPath{ "Data/SKSE/Plugins/Fellowship.ini" };
	std::mutex                  g_fileLock;

	std::string Trim(const std::string& s)
	{
		const auto first = s.find_first_not_of(" \t\r\n");
		if (first == std::string::npos) {
			return {};
		}
		const auto last = s.find_last_not_of(" \t\r\n");
		return s.substr(first, last - first + 1);
	}

	bool ParseBool(const std::string& v) { return v == "1" || v == "true" || v == "True" || v == "TRUE"; }

	const char* Bool(bool v) { return v ? "true" : "false"; }

	void LogSettings(const char* a_prefix)
	{
		logs::info("{}: enabled={} maxFollowers={} maxAnimals={} noFriendlyFire={} goldSharing={} followerCut={}% followerHomes={} "
			"sandboxWhenStill={} sandboxDelay={}s sandboxInInns={} verbose={}",
			a_prefix, Settings::enabled.load(), Settings::maxFollowers.load(), Settings::maxAnimals.load(), Settings::noFriendlyFire.load(),
			Settings::goldSharing.load(), Settings::followerCut.load(), Settings::followerHomes.load(), Settings::sandboxWhenStill.load(),
			Settings::sandboxDelay.load(), Settings::sandboxInInns.load(), Settings::verboseLog.load());
	}
}

void Settings::Load()
{
	std::scoped_lock lock{ g_fileLock };
	std::ifstream    file{ kPath };
	if (!file) {
		logs::warn("{} not found, using defaults", kPath.string());
		return;
	}

	std::string line;
	while (std::getline(file, line)) {
		line = Trim(line);
		if (line.empty() || line[0] == ';' || line[0] == '#' || line[0] == '[') {
			continue;
		}
		const auto eq = line.find('=');
		if (eq == std::string::npos) {
			continue;
		}
		const auto key = Trim(line.substr(0, eq));
		auto       value = Trim(line.substr(eq + 1));
		if (const auto comment = value.find(';'); comment != std::string::npos) {
			value = Trim(value.substr(0, comment));
		}

		try {
			if (key == "bEnabled") {
				enabled = ParseBool(value);
			} else if (key == "iMaxFollowers") {
				maxFollowers = std::clamp(std::stoi(value), 1, kMaxSlots);
			} else if (key == "iMaxAnimals") {
				maxAnimals = std::clamp(std::stoi(value), 1, kMaxSlots);
			} else if (key == "bNoFriendlyFire") {
				noFriendlyFire = ParseBool(value);
			} else if (key == "bGoldSharing") {
				goldSharing = ParseBool(value);
			} else if (key == "iFollowerCut") {
				followerCut = std::clamp(std::stoi(value), 1, kMaxCutPerFollower);
			} else if (key == "bFollowerHomes") {
				followerHomes = ParseBool(value);
			} else if (key == "bSandboxWhenStill") {
				sandboxWhenStill = ParseBool(value);
			} else if (key == "iSandboxDelay") {
				sandboxDelay = std::clamp(std::stoi(value), 1, kMaxSandboxDelay);
			} else if (key == "bSandboxInInns") {
				sandboxInInns = ParseBool(value);
			} else if (key == "bVerboseLog") {
				verboseLog = ParseBool(value);
			}
		} catch (const std::exception&) {
			logs::warn("Bad value for {}: '{}', keeping default", key, value);
		}
	}
	LogSettings("Settings loaded");
}

void Settings::Save()
{
	std::scoped_lock lock{ g_fileLock };
	{
		std::ofstream out{ kPath, std::ios::trunc };
		if (!out) {
			logs::error("Could not write {}", kPath.string());
			return;
		}
		out << "; Fellowship - settings\n"
			   "; Also editable in game: Mod Control Panel (SKSE Menu Framework) > Fellowship.\n"
			   "; The in-game menu rewrites this file whenever you change a setting.\n"
			   "\n"
			   "[General]\n"
			   "; Master switch. false = back to the vanilla one-follower limit (followers you already have stay).\n"
			<< "bEnabled = " << Bool(enabled) << "\n"
			<< "\n"
			   "; How many followers recruited the vanilla way can travel with you (1-" << kMaxSlots << "). Followers with their\n"
			   "; own systems (Inigo, Lucien, ...) don't count. Default 5\n"
			<< "iMaxFollowers = " << maxFollowers.load() << "\n"
			<< "\n"
			   "; How many animal companions (dogs, ...) can travel with you (1-" << kMaxSlots << "). A separate limit, as in\n"
			   "; vanilla: animals never take a follower's place. Default 5\n"
			<< "iMaxAnimals = " << maxAnimals.load() << "\n"
			<< "\n"
			   "; true = nobody in your party can hurt anyone: not you your followers, not them you or each other\n"
			   ";        (weapons, arrows, hostile spells), and nobody turns on you however often you hit them.\n"
			   "; false = vanilla: hits land, and a follower you keep hitting fights back and leaves. Default false\n"
			<< "bNoFriendlyFire = " << Bool(noFriendlyFire) << "\n"
			<< "\n"
			   "; true = your followers take a cut of everything you sell to merchants: you are offered less.\n"
			   ";        Animals don't take a cut. Default false\n"
			<< "bGoldSharing = " << Bool(goldSharing) << "\n"
			<< "\n"
			   "; Each follower's cut in percent (1-" << kMaxCutPerFollower << "). All of them together never take more than "
			<< kMaxTotalCut << "%. Default 5\n"
			<< "iFollowerCut = " << followerCut.load() << "\n"
			<< "\n"
			   "; true = followers and animals you recruited the vanilla way can be given a home: \"This is your home now.\"\n"
			   ";        Dismissed, they walk there and live around it. Homes you gave stay when this is off.\n"
			   ";        First time after installing: save, then load that save, or the lines won't show yet. Default false\n"
			<< "bFollowerHomes = " << Bool(followerHomes) << "\n"
			<< "\n"
			   "; true = followers and animals you recruited the vanilla way relax around you after you stand\n"
			   ";        still for a while, and follow again when you move. Default false\n"
			<< "bSandboxWhenStill = " << Bool(sandboxWhenStill) << "\n"
			<< "\n"
			   "; Seconds you stand still before they relax (1-" << kMaxSandboxDelay << "). Default 30\n"
			<< "iSandboxDelay = " << sandboxDelay.load() << "\n"
			<< "\n"
			   "; true = they also relax the whole time you are inside an inn or a player home. Default false\n"
			<< "bSandboxInInns = " << Bool(sandboxInInns) << "\n"
			<< "\n"
			   "; Extra detail in Documents/My Games/Skyrim Special Edition/SKSE/Fellowship.log\n"
			<< "bVerboseLog = " << Bool(verboseLog) << "\n";
		if (!out) {
			logs::error("Write to {} failed", kPath.string());
			return;
		}
	}
	LogSettings("Settings saved");
}

void Settings::ResetToDefaults()
{
	enabled = true;
	maxFollowers = kDefaultMaxMembers;
	maxAnimals = kDefaultMaxMembers;
	noFriendlyFire = false;
	goldSharing = false;
	followerCut = kDefaultFollowerCut;
	followerHomes = false;
	sandboxWhenStill = false;
	sandboxDelay = kDefaultSandboxDelay;
	sandboxInInns = false;
	verboseLog = false;
	Save();
}
