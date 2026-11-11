#pragma once

namespace Settings
{
	inline constexpr int kMaxSlots = 20;
	inline constexpr int kDefaultMaxMembers = 5;
	inline constexpr int kDefaultFollowerCut = 5;
	inline constexpr int kDefaultSandboxDelay = 30;

	inline std::atomic<bool> enabled{ true };
	inline std::atomic<int>  maxFollowers{ kDefaultMaxMembers };
	inline std::atomic<int>  maxAnimals{ kDefaultMaxMembers };
	inline std::atomic<bool> noFriendlyFire{ false };
	inline std::atomic<bool> goldSharing{ false };
	inline std::atomic<int>  followerCut{ kDefaultFollowerCut };
	inline constexpr int     kMaxCutPerFollower = 20;
	inline constexpr int     kMaxTotalCut = 50;
	inline std::atomic<bool> followerHomes{ false };
	inline std::atomic<bool> sandboxWhenStill{ false };
	inline std::atomic<int>  sandboxDelay{ kDefaultSandboxDelay };
	inline constexpr int     kMaxSandboxDelay = 120;
	inline std::atomic<bool> sandboxInInns{ false };
	inline std::atomic<bool> verboseLog{ false };

	void Load();
	void Save();
	void ResetToDefaults();
}
