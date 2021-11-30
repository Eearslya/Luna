#pragma once

namespace Luna {
template <typename T>
class Badge final {
	friend T;

 private:
	Badge() = default;
};
}  // namespace Luna
