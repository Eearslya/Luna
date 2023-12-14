#pragma once

#include <Luna/Common.hpp>
#include <Luna/Utility/Memory.hpp>

namespace Luna {
template <typename T>
class ObjectPool {
 public:
	template <typename... Args>
	T* Allocate(Args&&... args) {
		if (_available.empty()) {
			std::size_t newObjects = std::size_t(64) << _memory.size();
			T* ptr = static_cast<T*>(AllocateAligned(newObjects * sizeof(T), std::max<std::size_t>(64u, alignof(T))));
			if (!ptr) { return nullptr; }

			for (std::size_t i = 0; i < newObjects; ++i) { _available.push_back(&ptr[i]); }
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
	}

	void Free(T* ptr) {
		ptr->~T();
		_available.push_back(ptr);
	}

 protected:
	std::vector<T*> _available;
	std::vector<std::unique_ptr<T, AlignedDeleter>> _memory;
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
