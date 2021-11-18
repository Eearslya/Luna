#pragma once

#include <atomic>

#ifdef __SSE2__
#	include <emmintrin.h>
#endif

namespace Luna {
class RWSpinLock {
 public:
	constexpr static const int Reader = 2;
	constexpr static const int Writer = 1;

	RWSpinLock() {
		_counter.store(0);
	}

	void LockRead() {
		uint32_t v = _counter.fetch_add(Reader, std::memory_order_acquire);
		while ((v & Writer) != 0) {
#ifdef __SSE2__
			_mm_pause();
#endif
			v = _counter.load(std::memory_order_acquire);
		}
	}

	void LockWrite() {
		uint32_t expected = 0;
		while (!_counter.compare_exchange_weak(expected, Writer, std::memory_order_acquire, std::memory_order_relaxed)) {
#ifdef __SSE2__
			_mm_pause();
#endif
			expected = 0;
		}
	}

	void PromoteReaderToWriter() {
		uint32_t expected = Reader;
		if (!_counter.compare_exchange_strong(expected, Writer, std::memory_order_acquire, std::memory_order_relaxed)) {
			UnlockRead();
			LockWrite();
		}
	}

	void UnlockRead() {
		_counter.fetch_sub(Reader, std::memory_order_release);
	}

	void UnlockWrite() {
		_counter.fetch_and(~Writer, std::memory_order_release);
	}

 private:
	std::atomic_uint32_t _counter;
};
}  // namespace Luna
