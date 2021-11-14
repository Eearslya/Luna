#pragma once

#include <Luna/Utility/NonCopyable.hpp>

namespace Luna {
class Engine final : NonCopyable {
 public:
	Engine();
	~Engine() noexcept;

	int Run();
};
}  // namespace Luna
