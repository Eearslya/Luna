#pragma once

namespace Luna {
template <typename T>
class IntrusiveListEnabled {
 public:
	IntrusiveListEnabled<T>* Prev = nullptr;
	IntrusiveListEnabled<T>* Next = nullptr;
};

/**
 * Contains a doubly-linked list of objects. This class does not own the objects in the list.
 *
 * Objects used in this list must inherit from IntrusiveListEnabled.
 */
template <typename T>
class IntrusiveList {
 public:
	class Iterator {
	 public:
		Iterator() noexcept = default;
		Iterator(IntrusiveListEnabled<T>* node) noexcept : _node(node) {}

		[[nodiscard]] T* Get() noexcept {
			return static_cast<T*>(_node);
		}
		[[nodiscard]] const T* Get() const noexcept {
			return static_cast<const T*>(_node);
		}

		Iterator& operator++() noexcept {
			_node = _node->Next;
			return *this;
		}
		Iterator& operator--() noexcept {
			_node = _node->Prev;
			return *this;
		}

		[[nodiscard]] T& operator*() noexcept {
			return *static_cast<T*>(_node);
		}
		[[nodiscard]] const T& operator*() const noexcept {
			return *static_cast<const T*>(_node);
		}
		[[nodiscard]] T* operator->() noexcept {
			return static_cast<T*>(_node);
		}
		[[nodiscard]] const T* operator->() const noexcept {
			return static_cast<const T*>(_node);
		}

		[[nodiscard]] explicit operator bool() const noexcept {
			return _node != nullptr;
		}
		auto operator<=>(const Iterator& other) const noexcept = default;

	 private:
		IntrusiveListEnabled<T>* _node = nullptr;
	};

	[[nodiscard]] bool Empty() const noexcept {
		return _head = nullptr;
	}

	void Clear() noexcept {
		_head = nullptr;
		_tail = nullptr;
	}

	Iterator Erase(Iterator it) noexcept {
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
	void InsertBack(Iterator it) noexcept {
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
	void InsertFront(Iterator it) noexcept {
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

	void MoveToBack(IntrusiveList<T>& other, Iterator it) noexcept {
		other.Erase(it);
		InsertBack(it);
	}
	void MoveToFront(IntrusiveList<T>& other, Iterator it) noexcept {
		other.Erase(it);
		InsertFront(it);
	}

	[[nodiscard]] Iterator begin() const noexcept {
		return Iterator(_head);
	}
	[[nodiscard]] Iterator end() const noexcept {
		return Iterator();
	}
	[[nodiscard]] Iterator rbegin() const noexcept {
		return Iterator(_tail);
	}
	[[nodiscard]] Iterator rend() const noexcept {
		return Iterator();
	}

 private:
	IntrusiveListEnabled<T>* _head = nullptr;
	IntrusiveListEnabled<T>* _tail = nullptr;
};
}  // namespace Luna
