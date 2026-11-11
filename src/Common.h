#pragma once

namespace Common
{
	inline constexpr const char* kPlugin = "Fellowship.esp";
	inline constexpr const char* kScript = "Fellowship";

	inline const char* Name(RE::Actor* a_actor)
	{
		const char* name = a_actor ? a_actor->GetDisplayFullName() : nullptr;
		return name && *name ? name : "(unnamed)";
	}

	inline RE::Actor* AliasActor(RE::TESQuest* a_quest, std::uint32_t a_alias)
	{
		const auto ref = a_quest->GetAliasedRef(a_alias).get();
		return ref ? ref->As<RE::Actor>() : nullptr;
	}

	inline RE::Actor* HandleActor(RE::ObjectRefHandle a_handle)
	{
		const auto ref = a_handle.get();
		return ref ? ref->As<RE::Actor>() : nullptr;
	}

	inline void RunLater(std::chrono::milliseconds a_delay, std::function<void()> a_task)
	{
		std::thread([a_delay, a_task = std::move(a_task)]() {
			std::this_thread::sleep_for(a_delay);
			SKSE::GetTaskInterface()->AddTask(a_task);
		}).detach();
	}

	inline void StopFighting(RE::Actor* a_actor)
	{
		if (const auto lists = RE::ProcessLists::GetSingleton()) {
			lists->StopCombatAndAlarmOnActor(a_actor, false);
		}
		a_actor->StopAlarmOnActor();
		a_actor->EvaluatePackage();
	}

	inline void SetPerk(RE::PlayerCharacter* a_player, RE::BGSPerk* a_perk, bool a_want)
	{
		if (a_want == a_player->HasPerk(a_perk)) {
			return;
		}
		if (a_want) {
			a_player->AddPerk(a_perk);
		} else {
			a_player->RemovePerk(a_perk);
		}
		logs::info("Perk '{}' {}", a_perk->GetName(), a_want ? "added to the player" : "removed from the player");
	}

	class ScriptCallback : public RE::BSScript::IStackCallbackFunctor
	{
	public:
		explicit ScriptCallback(std::function<void()> a_onDone) :
			_onDone(std::move(a_onDone)) {}

		void operator()(RE::BSScript::Variable) override { SKSE::GetTaskInterface()->AddTask(_onDone); }
		void SetObject(const RE::BSTSmartPointer<RE::BSScript::Object>&) override {}

	private:
		std::function<void()> _onDone;
	};

	inline bool CallScript(const char* a_function, RE::BSScript::IFunctionArguments* a_args, std::function<void()> a_onDone)
	{
		const auto vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
		RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback;
		callback.reset(new ScriptCallback(std::move(a_onDone)));
		if (!vm || !vm->DispatchStaticCall(kScript, a_function, a_args, callback)) {
			logs::error("Papyrus call {}.{} failed (is Fellowship.pex installed?)", kScript, a_function);
			return false;
		}
		return true;
	}
}
