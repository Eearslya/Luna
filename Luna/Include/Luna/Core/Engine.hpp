#pragma once

namespace Luna {
class Engine final {
 public:
	Engine();
	~Engine() noexcept;

	int Run();
};
}  // namespace Luna
