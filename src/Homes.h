#pragma once

namespace Homes
{
	void OnDataLoaded();
	void OnGameLoaded();
	void Sync();

	struct Home
	{
		std::string name;
		RE::FormID  formID{ 0 };
		bool        withYou{ false };
		bool        loaded{ false };
	};
	std::vector<Home> GetHomes();
	void              Forget(RE::FormID a_follower);
	int               Capacity();
}
