#pragma once

#include <memory>
#include <mutex>
#include <vector>

#include "Memory.hpp"

namespace Luna {
template <typename T>
class ObjectPool {
 public:
	template <typename... Args>
	T* Allocate(Args&&... args) {
		if (_available.empty()) {
			const uint64_t objectCount = 64u << _memory.size();
			T* ptr = static_cast<T*>(AlignedAlloc(objectCount * sizeof(T), std::max(size_t(64), alignof(T))));
			if (!ptr) { return nullptr; }

			for (uint64_t i = 0; i < objectCount; ++i) { _available.push_back(&ptr[i]); }
			_memory.emplace_back(ptr);
		}

		T* ptr = _available.back();
		_available.pop_back();
		new (ptr) T(std::forward<Args>(args)...);

		return ptr;
	}

	void Free(T* ptr) {
		ptr->~T();
		_available.push_back(ptr);
	}

	void Clear() {
		_available.clear();
		_memory.clear();
	}

 protected:
	struct PoolDeleter {
		void operator()(T* ptr) {
			AlignedFree(ptr);
		}
	};

	std::vector<T*> _available;
	std::vector<std::unique_ptr<T, PoolDeleter>> _memory;
};

template <typename T>
class ThreadSafeObjectPool : private ObjectPool<T> {
 public:
	template <typename... Args>
	T* Allocate(Args&&... args) {
		std::lock_guard<std::mutex> lock(_mutex);
		return ObjectPool<T>::Allocate(std::forward<Args>(args)...);
	}

	void Free(T* ptr) {
		ptr->~T();
		std::lock_guard<std::mutex> lock(_mutex);
		this->_available.push_back(ptr);
	}

	void Clear() {
		std::lock_guard<std::mutex> lock(_mutex);
		ObjectPool<T>::Clear();
	}

 private:
	std::mutex _mutex;
};
}  // namespace Luna
