#pragma once

#include <Luna/Common.hpp>

namespace Luna {
int64_t GetCurrentTimeNanoseconds();

class Timer {
 public:
	void Start();
	double End() const;

 private:
	int64_t _t = 0;
};
}  // namespace Luna
