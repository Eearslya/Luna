#pragma once

#include <Luna/Utility/IntrusiveHashMap.hpp>
#include <vector>

namespace Luna {
template <typename T>
class TemporaryHashMapEnabled {
 public:
	Hash GetHash() const {
		return _hash;
	}
	uint32_t GetIndex() const {
		return _index;
	}

	void SetHash(Hash hash) {
		_hash = hash;
	}
	void SetIndex(uint32_t index) {
		_index = index;
	}

 private:
	Hash _hash      = 0;
	uint32_t _index = 0;
};

template <typename T, unsigned RingSize = 4, bool ReuseObjects = false>
class TemporaryHashMap {
 public:
	~TemporaryHashMap() noexcept {
		Clear();
	}

	void BeginFrame() {
		_index = (_index + 1) & (RingSize - 1);
		for (auto& node : _rings[_index]) {
			_hashMap.Erase(node.GetHash());
			FreeObject(&node, ReuseTag<ReuseObjects>());
		}
		_rings[_index].Clear();
	}

	void Clear() {
		for (auto& ring : _rings) {
			for (auto& node : ring) { _pool.Free(static_cast<T*>(&node)); }
			ring.Clear();
		}
		_hashMap.Clear();

		for (auto& vacant : _vacants) { _pool.Free(static_cast<T*>(&*vacant)); }
		_vacants.clear();
		_pool.Clear();
	}

	template <typename... Args>
	T* Emplace(Hash hash, Args&&... args) {
		auto* node = _pool.Allocate(std::forward<Args>(args)...);
		node->SetIndex(_index);
		node->SetHash(hash);
		_hashMap.EmplaceReplace(hash, node);
		_rings[_index].InsertFront(node);

		return node;
	}

	template <typename... Args>
	void MakeVacant(Args&&... args) {
		_vacants.push_back(_pool.Allocate(std::forward<Args>(args)...));
	}

	T* Request(Hash hash) {
		auto* v = _hashMap.Find(hash);
		if (v) {
			auto node = v->Value;
			if (node->GetIndex() != _index) {
				_rings[_index].MoveToFront(_rings[node->GetIndex()], node);
				node->SetIndex(_index);
			}

			return &*node;
		} else {
			return nullptr;
		}
	}

	T* RequestVacant(Hash hash) {
		if (_vacants.empty()) { return nullptr; }

		auto top = _vacants.back();
		_vacants.pop_back();
		top->SetIndex(_index);
		top->SetHash(hash);
		_hashMap.EmplaceReplace(hash, top);
		_rings[_index].InsertFront(top);

		return &*top;
	}

 private:
	template <bool reuse>
	struct ReuseTag {};

	void FreeObject(T* object, const ReuseTag<false>&) {
		_pool.Free(object);
	}
	void FreeObject(T* object, const ReuseTag<true>&) {
		_vacants.push_back(object);
	}

	IntrusiveHashMap<IntrusivePODWrapper<typename IntrusiveList<T>::Iterator>> _hashMap;
	uint32_t _index = 0;
	ObjectPool<T> _pool;
	IntrusiveList<T> _rings[RingSize];
	std::vector<typename IntrusiveList<T>::Iterator> _vacants;
};
}  // namespace Luna
