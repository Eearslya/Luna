#pragma once

#include <Luna/Core/Module.hpp>
#include <Luna/Utility/NonCopyable.hpp>
#include <map>

namespace Luna {
class Engine final : NonCopyable {
 public:
	Engine();
	~Engine() noexcept;

	static Engine* Get() {
		return _instance;
	}

	int Run();

 private:
	static Engine* _instance;

	std::multimap<Module::StageIndex, std::unique_ptr<Module>> _modules;
};
}  // namespace Luna
