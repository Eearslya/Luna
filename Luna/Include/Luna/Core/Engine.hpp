#pragma once

#include <Luna/Utility/NonCopyable.hpp>

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
};
}  // namespace Luna
