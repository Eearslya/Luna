#pragma once

#include <Luna/Core/Log.hpp>
#include <Luna/Utility/Memory.hpp>
#include <memory>
#include <mutex>
#include <vector>

namespace Luna {
template <typename T>
class ObjectPool {
 public:
	~ObjectPool() noexcept {
		if (_available.size() != _totalObjectCount) {
			Log::Error("[ObjectPool] Object pool is being destroyed while {} allocated objects still exist!", _totalObjectCount - _available.size());
		}
	}

	template <typename... Args>
	T* Allocate(Args&&... args) {
		if (_available.empty()) {
			const uint32_t objectCount = 64u << _memory.size();
			T* ptr = static_cast<T*>(AlignedAlloc(objectCount * sizeof(T), std::max(std::size_t(64), alignof(T))));
			if (!ptr) { return nullptr; }
			_totalObjectCount += objectCount;
			for (uint32_t i = 0; i < objectCount; ++i) { _available.push_back(&ptr[i]); }
			_memory.emplace_back(ptr);
		}

		T* ptr = _available.back();
		_available.pop_back();
		new (ptr) T(std::forward<Args>(args)...);

		return ptr;
	}

	void Clear() {
		_available.clear();
		_memory.clear();
		_totalObjectCount = 0;
	}

	void Free(T* ptr) {
		ptr->~T();
		_available.push_back(ptr);
	}

 protected:
	struct PoolDeleter {
		void operator()(T* ptr) {
			AlignedFree(ptr);
		}
	};

	std::vector<T*> _available;
	std::vector<std::unique_ptr<T, PoolDeleter>> _memory;
	size_t _totalObjectCount = 0;
};

template <typename T>
class ThreadSafeObjectPool : private ObjectPool<T> {
 public:
	template <typename... Args>
	T* Allocate(Args&&... args) {
		std::lock_guard<std::mutex> lock(_mutex);
		return ObjectPool<T>::Allocate(std::forward<Args>(args)...);
	}

	void Clear() {
		std::lock_guard<std::mutex> lock(_mutex);
		ObjectPool<T>::Clear();
	}

	void Free(T* ptr) {
		ptr->~T();
		std::lock_guard<std::mutex> lock(_mutex);
		this->_available.push_back(ptr);
	}

 private:
	std::mutex _mutex;
};
}  // namespace Luna
