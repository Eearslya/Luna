#pragma once

#include <Luna/Common.hpp>
#include <Luna/Utility/Hash.hpp>
#include <Luna/Utility/IntrusiveList.hpp>
#include <Luna/Utility/ObjectPool.hpp>
#include <Luna/Utility/SpinLock.hpp>

namespace Luna {
template <typename T>
class IntrusiveHashMapEnabled : public IntrusiveListEnabled<T> {
 public:
	IntrusiveHashMapEnabled() noexcept = default;
	IntrusiveHashMapEnabled(Hash hash) noexcept : _intrusiveHashMapKey(hash) {}

	Hash GetHash() const noexcept {
		return _intrusiveHashMapKey;
	}

	void SetHash(Hash hash) noexcept {
		_intrusiveHashMapKey = hash;
	}

 private:
	Hash _intrusiveHashMapKey = 0;
};

template <typename T>
struct IntrusivePODWrapper : public IntrusiveHashMapEnabled<IntrusivePODWrapper<T>> {
	IntrusivePODWrapper() noexcept = default;
	template <typename U>
	explicit IntrusivePODWrapper(U&& value) noexcept : Value(std::forward<U>(value)) {}

	T& Get() noexcept {
		return Value;
	}
	const T& Get() const noexcept {
		return Value;
	}

	T Value = {};
};

template <typename T>
class IntrusiveHashMapHolder {
 public:
	void Clear() noexcept {
		_list.Clear();
		_loadCount = 0;
		_values.clear();
	}

	T* Erase(Hash hash) noexcept {
		const Hash hashMask = _values.size() - 1;
		auto masked         = hash & hashMask;

		for (uint32_t i = 0; i < _loadCount; ++i) {
			if (_values[masked] && GetHash(_values[masked]) == hash) {
				auto* value = _values[masked];
				_list.Erase(value);
				_values[masked] = nullptr;

				return value;
			}

			masked = (masked + 1) & hashMask;
		}

		return nullptr;
	}

	void Erase(T* value) noexcept {
		Erase(GetHash(value));
	}

	[[nodiscard]] T* Find(Hash hash) const noexcept {
		if (_values.empty()) { return nullptr; }

		Hash hashMask = _values.size() - 1;
		auto masked   = hash & hashMask;
		for (uint32_t i = 0; i < _loadCount; ++i) {
			if (_values[masked] && GetHash(_values[masked]) == hash) { return _values[masked]; }
			masked = (masked + 1) & hashMask;
		}

		return nullptr;
	}

	template <typename P>
	bool FindAndConsumePOD(Hash hash, P& p) const noexcept {
		T* t = Find(hash);
		if (t) {
			p = t->Get();

			return true;
		}

		return false;
	}

	T* InsertReplace(T* value) noexcept {
		if (_values.empty()) { Grow(); }

		const Hash hashMask = _values.size() - 1;
		const Hash hash     = GetHash(value);
		auto masked         = hash & hashMask;

		for (uint32_t i = 0; i < _loadCount; ++i) {
			if (_values[masked] && GetHash(_values[masked]) == hash) {
				std::swap(_values[masked], value);
				_list.Erase(value);
				_list.InsertFront(_values[masked]);

				return value;
			} else if (!_values[masked]) {
				_values[masked] = value;
				_list.InsertFront(value);

				return nullptr;
			}

			masked = (masked + 1) & hashMask;
		}

		Grow();

		return InsertReplace(value);
	}

	T* InsertYield(T*& value) noexcept {
		if (_values.empty()) { Grow(); }

		const Hash hashMask = _values.size() - 1;
		const Hash hash     = GetHash(value);
		auto masked         = hash & hashMask;

		for (uint32_t i = 0; i < _loadCount; ++i) {
			if (_values[masked] && GetHash(_values[masked]) == hash) {
				T* ret = value;
				value  = _values[masked];

				return ret;
			} else if (!_values[masked]) {
				_values[masked] = value;
				_list.InsertFront(value);

				return nullptr;
			}

			masked = (masked + 1) & hashMask;
		}

		Grow();

		return InsertYield(value);
	}

	IntrusiveList<T>& InnerList() noexcept {
		return _list;
	}
	const IntrusiveList<T>& InnerList() const noexcept {
		return _list;
	}

	typename IntrusiveList<T>::Iterator begin() const noexcept {
		return _list.begin();
	}
	typename IntrusiveList<T>::Iterator end() const noexcept {
		return _list.end();
	}

 private:
	constexpr static int InitialSize      = 16;
	constexpr static int InitialLoadCount = 3;

	Hash GetHash(const T* value) const noexcept {
		return static_cast<const IntrusiveHashMapEnabled<T>*>(value)->GetHash();
	}
	void Grow() noexcept {
		bool success = false;
		do {
			for (auto& v : _values) { v = nullptr; }

			if (_values.empty()) {
				_values.resize(InitialSize);
				_loadCount = InitialLoadCount;
			} else {
				_values.resize(_values.size() * 2);
				_loadCount++;
			}

			success = true;
			for (auto& t : _list) {
				if (!InsertInner(&t)) {
					success = false;
					break;
				}
			}
		} while (!success);
	}
	bool InsertInner(T* value) noexcept {
		const Hash hashMask = _values.size() - 1;
		const Hash hash     = GetHash(value);
		auto masked         = hash & hashMask;

		for (uint32_t i = 0; i < _loadCount; ++i) {
			if (!_values[masked]) {
				_values[masked] = value;

				return true;
			}

			masked = (masked + 1) & hashMask;
		}

		return false;
	}

	IntrusiveList<T> _list;
	uint32_t _loadCount = 0;
	std::vector<T*> _values;
};

template <typename T>
class IntrusiveHashMap {
 public:
	IntrusiveHashMap() noexcept                          = default;
	IntrusiveHashMap(const IntrusiveHashMap&)            = delete;
	IntrusiveHashMap& operator=(const IntrusiveHashMap&) = delete;
	~IntrusiveHashMap() noexcept {
		Clear();
	}

	template <typename... Args>
	T* Allocate(Args&&... args) {
		return _objectPool.Allocate(std::forward<Args>(args)...);
	}

	void Clear() noexcept {
		auto& list = _hashMap.InnerList();
		auto it    = list.begin();
		while (it != list.end()) {
			auto* toFree = it.Get();
			it           = list.Erase(it);
			_objectPool.Free(toFree);
		}
		_hashMap.Clear();
	}

	template <typename... Args>
	T* EmplaceReplace(Hash hash, Args&&... args) {
		T* t = Allocate(std::forward<Args>(args)...);

		return InsertReplace(hash, t);
	}

	template <typename... Args>
	T* EmplaceYield(Hash hash, Args&&... args) {
		T* t = Allocate(std::forward<Args>(args)...);

		return InsertYield(hash, t);
	}

	void Erase(Hash hash) {
		auto* value = _hashMap.Erase(hash);
		if (value) { _objectPool.Free(value); }
	}

	void Erase(T* value) {
		_hashMap.Erase(value);
		_objectPool.Free(value);
	}

	T* Find(Hash hash) const noexcept {
		return _hashMap.Find(hash);
	}

	template <typename P>
	bool FindAndConsumePOD(Hash hash, P& p) const noexcept {
		return _hashMap.FindAndConsumePOD(hash, p);
	}

	void Free(T* value) {
		_objectPool.Free(value);
	}

	T* InsertReplace(Hash hash, T* value) {
		static_cast<IntrusiveHashMapEnabled<T>*>(value)->SetHash(hash);
		T* toDelete = _hashMap.InsertReplace(value);
		if (toDelete) { _objectPool.Free(toDelete); }

		return value;
	}

	T* InsertYield(Hash hash, T* value) {
		static_cast<IntrusiveHashMapEnabled<T>*>(value)->SetHash(hash);
		T* toDelete = _hashMap.InsertYield(value);
		if (toDelete) { _objectPool.Free(toDelete); }

		return value;
	}

	T& operator[](Hash hash) noexcept {
		auto* ret = Find(hash);
		if (!ret) { ret = EmplaceYield(hash); }

		return ret;
	}

	typename IntrusiveList<T>::Iterator begin() const {
		return _hashMap.begin();
	}

	typename IntrusiveList<T>::Iterator end() const {
		return _hashMap.end();
	}

 private:
	IntrusiveHashMapHolder<T> _hashMap;
	ObjectPool<T> _objectPool;
};

template <typename T>
using IntrusiveHashMapWrapper = IntrusiveHashMap<IntrusivePODWrapper<T>>;

template <typename T>
class ThreadSafeIntrusiveHashMap {
 public:
	template <typename... Args>
	T* Allocate(Args&&... args) {
		_spinLock.LockWrite();
		T* t = _hashMap.Allocate(std::forward<Args>(args)...);
		_spinLock.UnlockWrite();

		return t;
	}

	void Clear() noexcept {
		_spinLock.LockWrite();
		_hashMap.Clear();
		_spinLock.UnlockWrite();
	}

	template <typename... Args>
	T* EmplaceReplace(Hash hash, Args&&... args) {
		_spinLock.LockWrite();
		T* t = _hashMap.EmplaceReplace(hash, std::forward<Args>(args)...);
		_spinLock.UnlockWrite();

		return t;
	}

	template <typename... Args>
	T* EmplaceYield(Hash hash, Args&&... args) {
		_spinLock.LockWrite();
		T* t = _hashMap.EmplaceYield(hash, std::forward<Args>(args)...);
		_spinLock.UnlockWrite();

		return t;
	}

	void Erase(Hash hash) noexcept {
		_spinLock.LockWrite();
		_hashMap.Erase(hash);
		_spinLock.UnlockWrite();
	}

	void Erase(T* value) noexcept {
		_spinLock.LockWrite();
		_hashMap.Erase(value);
		_spinLock.UnlockWrite();
	}

	T* Find(Hash hash) const noexcept {
		_spinLock.LockRead();
		T* t = _hashMap.Find(hash);
		_spinLock.UnlockRead();

		return t;
	}

	template <typename P>
	bool FindAndConsumePOD(Hash hash, P& p) const noexcept {
		_spinLock.LockRead();
		bool ret = _hashMap.FindAndConsumePOD(hash, p);
		_spinLock.UnlockRead();

		return ret;
	}

	void Free(T* value) {
		_spinLock.LockWrite();
		_hashMap.Free(value);
		_spinLock.UnlockWrite();
	}

	T* InsertReplace(Hash hash, T* value) {
		_spinLock.LockWrite();
		value = _hashMap.InsertReplace(hash, value);
		_spinLock.UnlockWrite();

		return value;
	}

	T* InsertYield(Hash hash, T* value) {
		_spinLock.LockWrite();
		value = _hashMap.InsertYield(hash, value);
		_spinLock.UnlockWrite();

		return value;
	}

	typename IntrusiveList<T>::Iterator begin() const {
		return _hashMap.begin();
	}

	typename IntrusiveList<T>::Iterator end() const {
		return _hashMap.end();
	}

 private:
	IntrusiveHashMap<T> _hashMap;
	mutable RWSpinLock _spinLock;
};

template <typename T>
class ThreadSafeIntrusiveHashMapReadCached {
 public:
	~ThreadSafeIntrusiveHashMapReadCached() noexcept {
		Clear();
	}

	template <typename... Args>
	T* Allocate(Args&&... args) noexcept {
		_spinLock.LockWrite();
		T* t = _objectPool.Allocate(std::forward<Args>(args)...);
		_spinLock.UnlockWrite();

		return t;
	}

	void Clear() noexcept {
		_spinLock.LockWrite();
		ClearList(_readOnly.InnerList());
		ClearList(_readWrite.InnerList());
		_readOnly.Clear();
		_readWrite.Clear();
		_spinLock.UnlockWrite();
	}

	template <typename... Args>
	T* EmplaceYield(Hash hash, Args&&... args) noexcept {
		T* t = Allocate(std::forward<Args>(args)...);

		return InsertYield(hash, t);
	}

	T* Find(Hash hash) const noexcept {
		T* t = _readOnly.Find(hash);
		if (t) { return t; }

		_spinLock.LockRead();
		t = _readWrite.Find(hash);
		_spinLock.UnlockRead();

		return t;
	}

	template <typename P>
	bool FindAndConsumePOD(Hash hash, P& p) const noexcept {
		if (_readOnly.FindAndConsumePOD(hash, p)) { return true; }

		_spinLock.LockRead();
		bool ret = _readWrite.FindAndConsumePOD(hash, p);
		_spinLock.UnlockRead();

		return ret;
	}

	void Free(T* value) noexcept {
		_spinLock.LockWrite();
		_objectPool.Free(value);
		_spinLock.UnlockWrite();
	}

	IntrusiveHashMapHolder<T>& GetReadOnly() noexcept {
		return _readOnly;
	}

	IntrusiveHashMapHolder<T>& GetReadWrite() noexcept {
		return _readWrite;
	}

	T* InsertYield(Hash hash, T* value) noexcept {
		static_cast<IntrusiveHashMapEnabled<T>*>(value)->SetHash(hash);
		_spinLock.LockWrite();
		T* toDelete = _readWrite.InsertYield(value);
		if (toDelete) { _objectPool.Free(toDelete); }
		_spinLock.UnlockWrite();

		return value;
	}

	void MoveToReadOnly() noexcept {
		auto& list = _readWrite.InnerList();
		auto it    = list.begin();
		while (it != list.end()) {
			auto* toMove = it.Get();
			_readWrite.Erase(toMove);
			T* toDelete = _readOnly.InsertYield(toMove);
			if (toDelete) { _objectPool.Free(toDelete); }
			it = list.begin();
		}
	}

 private:
	void ClearList(IntrusiveList<T>& list) {
		auto it = list.begin();
		while (it != list.end()) {
			auto* toFree = it.Get();
			it           = list.Erase(it);
			_objectPool.Free(toFree);
		}
	}

	ObjectPool<T> _objectPool;
	IntrusiveHashMapHolder<T> _readOnly;
	IntrusiveHashMapHolder<T> _readWrite;
	mutable RWSpinLock _spinLock;
};
}  // namespace Luna
