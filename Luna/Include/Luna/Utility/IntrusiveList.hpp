#pragma once

namespace Luna {
template <typename T>
class IntrusiveList;

template <typename T>
class IntrusiveListEnabled {
	friend class IntrusiveList<T>;

 private:
	IntrusiveListEnabled<T>* _listPrev = nullptr;
	IntrusiveListEnabled<T>* _listNext = nullptr;
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
			return static_cast<T*>(_node);
		}

		explicit operator bool() const {
			return _node != nullptr;
		}
		bool operator==(const Iterator& other) const {
			return _node == other._node;
		}
		bool operator!=(const Iterator& other) const {
			return _node != other._node;
		}

		T& operator*() {
			return *static_cast<T*>(_node);
		}
		const T& operator*() const {
			return *static_cast<T*>(_node);
		}
		T* operator->() {
			return static_cast<T*>(_node);
		}
		const T* operator->() const {
			return static_cast<T*>(_node);
		}

		Iterator& operator++() {
			_node = _node->_listNext;
			return *this;
		}
		Iterator& operator--() {
			_node = _node->_listPrev;
			return *this;
		}

	 private:
		IntrusiveListEnabled<T>* _node = nullptr;
	};

	Iterator begin() {
		return Iterator(_head);
	}
	Iterator end() {
		return Iterator();
	}
	Iterator rbegin() {
		return Iterator(_tail);
	}
	Iterator rend() {
		return Iterator();
	}

	void Clear() {
		_head = nullptr;
		_tail = nullptr;
	}
	bool Empty() const {
		return _head == nullptr;
	}
	Iterator Erase(Iterator it) {
		auto* node = it.Get();
		auto* next = node->_listNext;
		auto* prev = node->_listPrev;

		if (prev) {
			prev->_listNext = next;
		} else {
			_head = next;
		}

		if (next) {
			next->_listPrev = prev;
		} else {
			_tail = prev;
		}

		return next;
	}

	void InsertFront(Iterator it) {
		auto* node = it.Get();

		if (_head) {
			_head->_listPrev = node;
		} else {
			_tail = node;
		}

		node->_listNext = _head;
		node->_listPrev = nullptr;
		_head           = node;
	}
	void InsertBack(Iterator it) {
		auto* node = it.Get();

		if (_tail) {
			_tail->_listNext = node;
		} else {
			_head = node;
		}

		node->_listNext = nullptr;
		node->_listPrev = _tail;
		_tail           = node;
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
