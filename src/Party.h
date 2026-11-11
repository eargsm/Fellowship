#pragma once

namespace Party
{
	void RegisterSerialization();
	void OnDataLoaded();

	struct Member
	{
		std::string name;
		RE::FormID  formID{ 0 };
		int         slot{ -1 };
		bool        waiting{ false };
		bool        loaded{ false };
		float       waitDaysLeft{ -1.0f };
	};

	struct GroupStatus
	{
		int                 count{ 0 };
		std::vector<Member> members;
	};

	struct Status
	{
		bool        ready{ false };
		GroupStatus followers;
		GroupStatus animals;
		int         goldFollowers{ 0 };
		std::string lastEvent;
	};

	Status GetStatus();

	void CalmEveryone();
	void SyncNow();
}
