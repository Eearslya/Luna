#pragma once

#include <atomic>
#include <type_traits>

namespace Luna {
class SingleThreadCounter {
 public:
	void AddReference() noexcept {
		_count++;
	}

	bool ReleaseReference() noexcept {
		return --_count == 0;
	}

 private:
	std::size_t _count = 1;
};

class MultiThreadCounter {
 public:
	MultiThreadCounter() noexcept {
		_count.store(1, std::memory_order_relaxed);
	}

	void AddReference() noexcept {
		_count.fetch_add(1, std::memory_order_relaxed);
	}

	bool ReleaseReference() noexcept {
		return _count.fetch_sub(1, std::memory_order_acq_rel) == 1;
	}

 private:
	std::atomic_size_t _count;
};

template <typename T>
class IntrusivePtr;

template <typename T, typename DeleterT = std::default_delete<T>, typename ReferenceOpsT = SingleThreadCounter>
class IntrusivePtrEnabled {
 public:
	using IntrusivePtrT        = IntrusivePtr<T>;
	using EnabledBaseT         = T;
	using EnabledDeleterT      = DeleterT;
	using EnabledReferenceOpsT = ReferenceOpsT;

	IntrusivePtrEnabled() noexcept                             = default;
	IntrusivePtrEnabled(const IntrusivePtrEnabled&)            = delete;
	IntrusivePtrEnabled& operator=(const IntrusivePtrEnabled&) = delete;

	void AddReference() noexcept {
		_referenceCount.AddReference();
	}
	void ReleaseReference() noexcept {
		if (_referenceCount.ReleaseReference()) { DeleterT()(static_cast<T*>(this)); }
	}

 protected:
	IntrusivePtrT ReferenceFromThis();

 private:
	ReferenceOpsT _referenceCount;
};

template <typename T>
class IntrusivePtr {
 public:
	template <typename U>
	friend class IntrusivePtr;

	IntrusivePtr() noexcept = default;
	explicit IntrusivePtr(T* data) : _data(data) {}

	template <typename U>
	IntrusivePtr(const IntrusivePtr<U>& other) {
		*this = other;
	}
	IntrusivePtr(const IntrusivePtr& other) {
		*this = other;
	}

	template <typename U>
	IntrusivePtr(IntrusivePtr<U>&& other) noexcept {
		*this = std::move(other);
	}
	IntrusivePtr(IntrusivePtr&& other) noexcept {
		*this = std::move(other);
	}

	~IntrusivePtr() noexcept {
		Reset();
	}

	template <typename U>
	IntrusivePtr& operator=(const IntrusivePtr<U>& other) noexcept {
		static_assert(std::is_base_of_v<T, U>, "Cannot safely assign downcasted intrusive pointers");

		using ReferenceBaseT =
			IntrusivePtrEnabled<typename T::EnabledBaseT, typename T::EnabledDeleterT, typename T::EnabledReferenceOpsT>;

		Reset();
		_data = static_cast<T*>(other._data);
		if (_data) { static_cast<ReferenceBaseT*>(_data)->AddReference(); }

		return *this;
	}
	IntrusivePtr& operator=(const IntrusivePtr& other) noexcept {
		using ReferenceBaseT =
			IntrusivePtrEnabled<typename T::EnabledBaseT, typename T::EnabledDeleterT, typename T::EnabledReferenceOpsT>;

		if (this != &other) {
			Reset();
			_data = other._data;
			if (_data) { static_cast<ReferenceBaseT*>(_data)->AddReference(); }
		}

		return *this;
	}

	template <typename U>
	IntrusivePtr& operator=(IntrusivePtr<U>&& other) noexcept {
		Reset();
		_data       = other._data;
		other._data = nullptr;

		return *this;
	}
	IntrusivePtr& operator=(IntrusivePtr&& other) noexcept {
		if (this != &other) {
			Reset();
			_data       = other._data;
			other._data = nullptr;
		}

		return *this;
	}

	[[nodiscard]] T* Get() noexcept {
		return _data;
	}
	[[nodiscard]] const T* Get() const noexcept {
		return _data;
	}
	T* Release() & noexcept {
		T* data = _data;
		_data   = nullptr;

		return data;
	}
	T* Release() && noexcept {
		T* data = _data;
		_data   = nullptr;

		return data;
	}

	void Reset() noexcept {
		using ReferenceBaseT =
			IntrusivePtrEnabled<typename T::EnabledBaseT, typename T::EnabledDeleterT, typename T::EnabledReferenceOpsT>;

		if (_data) { static_cast<ReferenceBaseT*>(_data)->ReleaseReference(); }

		_data = nullptr;
	}

	[[nodiscard]] explicit operator bool() const noexcept {
		return _data != nullptr;
	}
	[[nodiscard]] T& operator*() noexcept {
		return *_data;
	}
	[[nodiscard]] const T& operator*() const noexcept {
		return *_data;
	}
	[[nodiscard]] T* operator->() noexcept {
		return _data;
	}
	[[nodiscard]] const T* operator->() const noexcept {
		return _data;
	}

	[[nodiscard]] bool operator==(const IntrusivePtr& other) const noexcept {
		return _data == other._data;
	}
	[[nodiscard]] bool operator!=(const IntrusivePtr& other) const noexcept {
		return _data != other._data;
	}

 private:
	T* _data = nullptr;
};

template <typename T, typename DeleterT, typename ReferenceOpsT>
IntrusivePtr<T> IntrusivePtrEnabled<T, DeleterT, ReferenceOpsT>::ReferenceFromThis() {
	AddReference();

	return IntrusivePtr<T>(static_cast<T*>(this));
}

template <typename Derived>
using DerivedIntrusivePtrT = IntrusivePtr<Derived>;

template <typename T, typename... Args>
DerivedIntrusivePtrT<T> MakeHandle(Args&&... args) {
	return DerivedIntrusivePtrT<T>(new T(std::forward<Args>(args)...));
}

template <typename BaseT, typename DerivedT, typename... Args>
typename BaseT::IntrusivePtrT MakeDerivedHandle(Args&&... args) {
	return typename BaseT::IntrusivePtrT(new DerivedT(std::forward<Args>(args)...));
}

template <typename T, typename DeleterT = std::default_delete<T>>
using ThreadSafeIntrusivePtrEnabled = IntrusivePtrEnabled<T, DeleterT, MultiThreadCounter>;
}  // namespace Luna
