#pragma once

#include <atomic>

#ifdef __SSE2__
#	include <emmintrin.h>
#endif

namespace Luna {
class RWSpinLock {
 public:
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

	bool TryLockRead() {
		uint32_t v = _counter.fetch_add(Reader, std::memory_order_acquire);
		if ((v & Writer) != 0) {
			UnlockRead();
			return false;
		}

		return true;
	}

	void UnlockRead() {
		_counter.fetch_sub(Reader, std::memory_order_release);
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

	void UnlockWrite() {
		_counter.fetch_and(~Writer, std::memory_order_release);
	}

	bool TryLockWrite() {
		uint32_t expected = 0;
		return _counter.compare_exchange_strong(expected, Writer, std::memory_order_acquire, std::memory_order_relaxed);
	}

	void PromoteReaderToWriter() {
		uint32_t expected = Reader;
		if (!_counter.compare_exchange_strong(expected, Writer, std::memory_order_acquire, std::memory_order_relaxed)) {
			UnlockRead();
			LockWrite();
		}
	}

 private:
	constexpr static int Writer = 1;
	constexpr static int Reader = 2;

	std::atomic_uint32_t _counter;
};

class RWSpinLockReadHolder {
 public:
	explicit RWSpinLockReadHolder(RWSpinLock& lock) : _lock(lock) {
		_lock.LockRead();
	}

	~RWSpinLockReadHolder() noexcept {
		_lock.UnlockRead();
	}

 private:
	RWSpinLock& _lock;
};

class RWSpinLockWriteHolder {
 public:
	explicit RWSpinLockWriteHolder(RWSpinLock& lock) : _lock(lock) {
		_lock.LockWrite();
	}

	~RWSpinLockWriteHolder() noexcept {
		_lock.UnlockWrite();
	}

 private:
	RWSpinLock& _lock;
};
}  // namespace Luna
