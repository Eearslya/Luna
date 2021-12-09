#pragma once

#include <Luna/Utility/Hash.hpp>
#include <Luna/Utility/IntrusiveList.hpp>
#include <Luna/Utility/NonCopyable.hpp>
#include <Luna/Utility/ObjectPool.hpp>
#include <Luna/Utility/SpinLock.hpp>

namespace Luna {
template <typename T>
class IntrusiveHashMapEnabled : public IntrusiveListEnabled<T> {
 public:
	IntrusiveHashMapEnabled() = default;
	IntrusiveHashMapEnabled(Hash hash) : _intrusiveHashmapKey(hash) {}

	Hash GetHash() const {
		return _intrusiveHashmapKey;
	}

	void SetHash(Hash hash) {
		_intrusiveHashmapKey = hash;
	}

 private:
	Hash _intrusiveHashmapKey = 0;
};

template <typename T>
struct IntrusivePODWrapper : public IntrusiveHashMapEnabled<IntrusivePODWrapper<T>> {
	IntrusivePODWrapper() = default;
	template <typename U>
	explicit IntrusivePODWrapper(U&& value) : Value(std::forward<U>(value)) {}

	T Value = {};
};

template <typename T>
class IntrusiveHashMapHolder {
 public:
	constexpr static const int InitialLoadCount = 3;
	constexpr static const int InitialSize      = 16;

	typename IntrusiveList<T>::Iterator begin() {
		return _list.begin();
	}

	typename IntrusiveList<T>::Iterator end() {
		return _list.end();
	}

	void Clear() {
		_list.Clear();
		_values.clear();
		_loadCount = 0;
	}

	T* Erase(Hash hash) {
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

	T* Erase(T* value) {
		Erase(GetHash(value));
	}

	T* Find(Hash hash) const {
		if (_values.empty()) { return nullptr; }

		const Hash hashMask = _values.size() - 1;
		auto masked         = hash & hashMask;
		for (uint32_t i = 0; i < _loadCount; ++i) {
			auto& value = _values[masked];
			if (value && GetHash(value) == hash) { return value; }
			masked = (masked + 1) & hashMask;
		}

		return nullptr;
	}

	template <typename P>
	bool FindAndConsumePOD(Hash hash, P& p) const {
		T* value = Find(hash);
		if (value) {
			p = value->Value;

			return true;
		}

		return false;
	}

	IntrusiveList<T>& InnerList() {
		return _list;
	}

	T* InsertReplace(T* value) {
		if (_values.empty()) { Grow(); }

		const Hash hashMask = _values.size() - 1;
		const auto hash     = GetHash(value);
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

	T* InsertYield(T*& value) {
		if (_values.empty()) { Grow(); }

		const Hash hashMask = _values.size() - 1;
		const auto hash     = GetHash(value);
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

 private:
	bool CompareKey(Hash masked, Hash hash) const {
		return GetKeyForIndex(masked) == hash;
	}

	Hash GetHash(const T* value) const {
		return static_cast<const IntrusiveHashMapEnabled<T>*>(value)->GetHash();
	}

	Hash GetKeyForIndex(Hash masked) const {
		return GetHash(_values[masked]);
	}

	void Grow() {
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

	bool InsertInner(T* value) {
		const Hash hashMask = _values.size() - 1;
		const auto hash     = GetHash(value);
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
class IntrusiveHashMap : NonCopyable {
 public:
	IntrusiveHashMap() = default;
	~IntrusiveHashMap() noexcept {
		Clear();
	}

	template <typename... Args>
	T* Allocate(Args&&... args) {
		_pool.Allocate(std::forward<Args>(args)...);
	}

	void Clear() {
		auto& list = _hashMap.InnerList();
		auto it    = list.begin();
		while (it != list.end()) {
			auto* toFree = it.Get();
			it           = list.erase(it);
			_pool.Free(toFree);
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
		if (value) { _pool.Free(value); }
	}

	void Erase(T* value) {
		_hashMap.Erase(value);
		_pool.Free(value);
	}

	T* Find(Hash hash) const {
		return _hashMap.Find(hash);
	}

	void Free(T* value) {
		_pool.Free(value);
	}

	template <typename P>
	bool FindAndConsumePOD(Hash hash, P& p) const {
		return _hashMap.FindAndConsumePod(hash, p);
	}

	IntrusiveHashMap<T>& GetThreadUnsafe() {
		return *this;
	}

	T* InsertReplace(Hash hash, T* value) {
		static_cast<IntrusiveHashMapEnabled<T>*>(value)->SetHash(hash);
		T* toDelete = _hashMap.InsertReplace(value);
		if (toDelete) { _pool.Free(toDelete); }

		return value;
	}

	T* InsertYield(Hash hash, T* value) {
		static_cast<IntrusiveHashMapEnabled<T>*>(value)->SetHash(hash);
		T* toDelete = _hashMap.InsertYield(value);
		if (toDelete) { _pool.Free(toDelete); }

		return value;
	}

	typename IntrusiveList<T>::Iterator begin() {
		return _hashMap.begin();
	}

	typename IntrusiveList<T>::Iterator end() {
		return _hashMap.end();
	}

	T& operator[](Hash hash) {
		auto* t = Find(hash);
		if (!t) { t = EmplaceYield(hash); }

		return *t;
	}

 private:
	IntrusiveHashMapHolder<T> _hashMap;
	ObjectPool<T> _pool;
};

template <typename T>
using IntrusiveHashMapWrapper = IntrusiveHashMap<IntrusivePODWrapper<T>>;

template <typename T>
class ThreadSafeIntrusiveHashMap {
 public:
	template <typename... Args>
	T* Allocate(Args&&... args) {
		_lock.LockWrite();
		T* t = _hashMap.Allocate(std::forward<Args>(args)...);
		_lock.UnlockWrite();

		return t;
	}

	void Clear() {
		_lock.LockWrite();
		_hashMap.Clear();
		_lock.UnlockWrite();
	}

	void Erase(Hash hash) {
		_lock.LockWrite();
		_hashMap.Erase(hash);
		_lock.UnlockWrite();
	}

	void Erase(T* value) {
		_lock.LockWrite();
		_hashMap.Erase(value);
		_lock.UnlockWrite();
	}

	T* Find(Hash hash) const {
		_lock.LockRead();
		T* t = _hashMap.Find(hash);
		_lock.UnlockRead();

		return t;
	}

	void Free(T* value) {
		_lock.LockWrite();
		_hashMap.Free(value);
		_lock.UnlockWrite();
	}

	T* InsertReplace(Hash hash, T* value) {
		_lock.LockWrite();
		value = _hashMap.InsertReaplce(hash, value);
		_lock.UnlockWrite();

		return value;
	}

	T* InsertYield(Hash hash, T* value) {
		_lock.LockWrite();
		value = _hashMap.InsertYield(hash, value);
		_lock.UnlockWrite();

		return value;
	}

	template <typename... Args>
	T* EmplaceReplace(Hash hash, Args&&... args) {
		_lock.LockWrite();
		T* t = _hashMap.EmplaceReplace(hash, std::forward<Args>(args)...);
		_lock.UnlockWrite();

		return t;
	}

	template <typename... Args>
	T* EmplaceYield(Hash hash, Args&&... args) {
		_lock.LockWrite();
		T* t = _hashMap.EmplaceYield(hash, std::forward<Args>(args)...);
		_lock.UnlockWrite();

		return t;
	}

	template <typename P>
	bool FindAndConsumePOD(Hash hash, P& p) const {
		_lock.LockRead();
		bool ret = _hashMap.FindAndConsumePOD(hash, p);
		_lock.UnlockRead();

		return ret;
	}

	IntrusiveHashMap<T>& GetThreadUnsafe() {
		return _hashMap;
	}

	typename IntrusiveList<T>::Iterator begin() {
		return _hashMap.begin();
	}

	typename IntrusiveList<T>::Iterator end() {
		return _hashMap.end();
	}

 private:
	IntrusiveHashMap<T> _hashMap;
	mutable RWSpinLock _lock;
};

template <typename T>
class ThreadSafeIntrusiveHashMapReadCached {
 public:
	~ThreadSafeIntrusiveHashMapReadCached() noexcept {
		Clear();
	}

	template <typename... Args>
	T* Allocate(Args&&... args) {
		_lock.LockWrite();
		T* t = _pool.Allocate(std::forward<Args>(args)...);
		_lock.UnlockWrite();

		return t;
	}

	void Clear() {
		_lock.LockWrite();
		ClearList(_readOnly.InnerList());
		ClearList(_readWrite.InnerList());
		_readOnly.Clear();
		_readWrite.Clear();
		_lock.UnlockWrite();
	}

	template <typename... Args>
	T* EmplaceYield(Hash hash, Args&&... args) {
		T* t = Allocate(std::forward<Args>(args)...);

		return InsertYield(hash, t);
	}

	T* Find(Hash hash) const {
		T* t = _readOnly.Find(hash);
		if (t) { return t; }

		_lock.LockRead();
		t = _readWrite.Find(hash);
		_lock.UnlockRead();

		return t;
	}

	template <typename P>
	bool FindAndConsumePOD(Hash hash, P& p) const {
		if (_readOnly.FindAndConsumePOD(hash, p)) { return true; }

		_lock.LockRead();
		bool ret = _readWrite.FindAndConsumePOD(hash, p);
		_lock.UnlockRead();

		return ret;
	}

	void Free(T* ptr) {
		_lock.LockWrite();
		_pool.Free(ptr);
		_lock.UnlockWrite();
	}

	IntrusiveHashMapHolder<T>& GetReadOnly() {
		return _readOnly;
	}

	IntrusiveHashMapHolder<T>& GetReadWrite() {
		return _readWrite;
	}

	T* InsertYield(Hash hash, T* value) {
		static_cast<IntrusiveHashMapEnabled<T>*>(value)->SetHash(hash);
		_lock.LockWrite();
		T* toDelete = _readWrite.InsertYield(value);
		if (toDelete) { _pool.Free(toDelete); }
		_lock.UnlockWrite();

		return value;
	}

	void MoveToReadOnly() {
		auto& list = _readWrite.InnerList();
		auto it    = list.begin();
		while (it != list.end()) {
			auto* toMove = it.Get();
			_readWrite.Erase(toMove);
			T* toDelete = _readOnly.InsertYield(toMove);
			if (toDelete) { _pool.Free(toDelete); }
			it = list.begin();
		}
	}

 private:
	void ClearList(IntrusiveList<T>& list) {
		auto it = list.begin();
		while (it != list.end()) {
			auto* toFree = it.Get();
			it           = list.Erase(it);
			_pool.Free(toFree);
		}
	}

	mutable RWSpinLock _lock;
	IntrusiveHashMapHolder<T> _readOnly;
	IntrusiveHashMapHolder<T> _readWrite;
	ObjectPool<T> _pool;
};
}  // namespace Luna
