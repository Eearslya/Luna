#pragma once

namespace Luna {
template <typename T>
struct IntrusiveListEnabled {
	IntrusiveListEnabled<T>* Prev = nullptr;
	IntrusiveListEnabled<T>* Next = nullptr;
};

template <typename T>
class IntrusiveList {
 public:
	class Iterator {
		friend class IntrusiveList<T>;

	 public:
		Iterator() = default;
		Iterator(IntrusiveListEnabled<T>* node) : _node(node) {}

		T* Get() {
			return static_cast<T*>(_node);
		}
		const T* Get() const {
			return static_cast<const T*>(_node);
		}

		T* operator->() {
			return Get();
		}
		const T* operator->() const {
			return Get();
		}
		T& operator*() {
			return *Get();
		}
		const T& operator*() const {
			return *Get();
		}
		explicit operator bool() const {
			return _node != nullptr;
		}
		Iterator& operator++() {
			_node = _node->Next;
			return *this;
		}
		Iterator& operator--() {
			_node = _node->Prev;
			return *this;
		}

		bool operator==(const Iterator& other) const {
			return _node == other._node;
		}
		bool operator!=(const Iterator& other) const {
			return _node != other._node;
		}

	 private:
		IntrusiveListEnabled<T>* _node = nullptr;
	};

	Iterator begin() const {
		return Iterator(_head);
	}
	Iterator rbegin() const {
		return Iterator(_tail);
	}
	Iterator end() const {
		return Iterator();
	}
	Iterator rend() const {
		return Iterator();
	}

	bool Empty() const {
		return _head == nullptr;
	}

	void Clear() {
		_head = nullptr;
		_tail = nullptr;
	}

	Iterator Erase(Iterator it) {
		auto* node = it.Get();
		auto* next = node->Next;
		auto* prev = node->Prev;

		if (prev) {
			prev->Next = next;
		} else {
			_head = next;
		}

		if (next) {
			next->Prev = prev;
		} else {
			_tail = prev;
		}

		return next;
	}

	void InsertFront(Iterator it) {
		auto* node = it.Get();
		if (_head) {
			_head->Prev = node;
		} else {
			_tail = node;
		}

		node->Next = _head;
		node->Prev = nullptr;
		_head      = node;
	}

	void InsertBack(Iterator it) {
		auto* node = it.Get();
		if (_tail) {
			_tail->Next = node;
		} else {
			_head = node;
		}

		node->Prev = _tail;
		node->Next = nullptr;
		_tail      = node;
	}

	void MoveToFront(IntrusiveList<T>& other, Iterator it) {
		other.Erase(it);
		InsertFront(it);
	}

	void MoveToBack(IntrusiveList<T>& other, Iterator it) {
		other.Erase(it);
		InsertBack(it);
	}

 private:
	IntrusiveListEnabled<T>* _head = nullptr;
	IntrusiveListEnabled<T>* _tail = nullptr;
};
}  // namespace Luna
