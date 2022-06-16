#pragma once

#include <atomic>

namespace Luna {
class RefCounted {
 public:
	void AddReference() const {
		++_refCount;
	}
	void ReleaseReference() const {
		--_refCount;
	}
	uint32_t GetReferenceCount() const {
		return _refCount.load();
	}

 private:
	mutable std::atomic_uint32_t _refCount = 0;
};

template <typename T>
class Ref {
	static_assert(std::is_base_of_v<RefCounted, T>, "Ref objects must inherit from RefCounted!");

 public:
	Ref() : _instance(nullptr) {}
	Ref(std::nullptr_t) : _instance(nullptr) {}
	Ref(T* instance) : _instance(instance) {
		AddReference();
	}
	Ref(const Ref& other) {
		_instance = other._instance;
		AddReference();
	}
	template <typename U>
	Ref(const Ref<U>& other) {
		_instance = reinterpret_cast<T*>(other._instance);
		AddReference();
	}
	Ref(Ref&& other) noexcept {
		_instance       = other._instance;
		other._instance = nullptr;
	}
	template <typename U>
	Ref(Ref<U>&& other) noexcept {
		_instance       = reinterpret_cast<T*>(other._instance);
		other._instance = nullptr;
	}
	~Ref() noexcept {
		ReleaseReference();
	}

	Ref& operator=(std::nullptr_t) {
		ReleaseReference();
		_instance = nullptr;
		return *this;
	}
	Ref& operator=(const Ref& other) {
		other.AddReference();
		ReleaseReference();
		_instance = other._instance;
		return *this;
	}
	template <typename U>
	Ref& operator=(const Ref<U>& other) {
		other.AddReference();
		ReleaseReference();
		_instance = reinterpret_cast<T*>(other._instance);
		return *this;
	}
	Ref& operator=(Ref&& other) noexcept {
		ReleaseReference();
		_instance       = other._instance;
		other._instance = nullptr;
		return *this;
	}
	template <typename U>
	Ref& operator=(Ref<U>&& other) noexcept {
		ReleaseReference();
		_instance       = reinterpret_cast<T*>(other._instance);
		other._instance = nullptr;
		return *this;
	}

	operator bool() const {
		return _instance != nullptr;
	}
	bool operator==(const Ref& other) const {
		return _instance == other._instance;
	}
	bool operator!=(const Ref& other) const {
		return _instance != other._instance;
	}
	T* operator->() {
		return _instance;
	}
	const T* operator->() const {
		return _instance;
	}
	T& operator*() {
		return *_instance;
	}
	const T& operator*() const {
		return *_instance;
	}

	template <typename U>
	Ref<U> As() const {
		return Ref<U>(*this);
	}
	T* Get() {
		return _instance;
	}
	const T* Get() const {
		return _instance;
	}
	void Reset(T* instance = nullptr) {
		ReleaseReference();
		_instance = instance;
	}

	template <typename... Args>
	static Ref<T> Create(Args&&... args) {
		return Ref<T>(new T(std::forward<Args>(args)...));
	}

 private:
	void AddReference() const {
		if (_instance) { _instance->AddReference(); }
	}
	void ReleaseReference() const {
		if (_instance) {
			_instance->ReleaseReference();
			if (_instance->GetReferenceCount() == 0) {
				delete _instance;
				_instance = nullptr;
			}
		}
	}

	mutable T* _instance = nullptr;
};
}  // namespace Luna
