#pragma once

#include <Luna/Common.hpp>

namespace Luna {
template <typename>
class Delegate;
template <typename...>
class CancellableDelegate;

// A simple class that contains a shared pointer. This is used to track object lifetimes in delegate callbacks. If the
// object that extends Observer is destroyed, the shared pointer will be destroyed as well, invalidating all of the weak
// references stored in the delegate callback. This can be used to ensure any delegate callbacks that rely on an
// object's lifetime are removed when the object is destroyed.
class Observer {
 public:
	Observer() : ObserverIsAlive(std::make_shared<bool>(true)) {}
	virtual ~Observer() noexcept = default;

	std::shared_ptr<bool> ObserverIsAlive;
};

template <typename T>
concept IsObserver = std::is_convertible_v<typename std::remove_reference<T>::type*, Observer*>;

// Template class used to invoke delegates that return some type of value. The invoker will look for any delegate
// callbacks that have expired observers and remove them from the delegate.
template <typename ReturnT, typename... Args>
class Invoker {
 public:
	using ReturnValuesT = std::vector<ReturnT>;

	static ReturnValuesT Invoke(Delegate<ReturnT(Args...)>& delegate, Args&&... args) {
		std::lock_guard<std::mutex> lock(delegate._mutex);

		ReturnValuesT values;
		for (auto it = delegate._functions.begin(); it != delegate._functions.end();) {
			if (it->IsExpired()) {
				it = delegate._functions.erase(it);
				continue;
			}

			values.emplace_back((*it->Function)(std::forward<Args>(args)...));
			++it;
		}

		return values;
	}
};

// Template class used to invoke delegates that have no return value. The invoker will look for any delegate callbacks
// that have expired and remove them from the delegate.
template <typename... Args>
class Invoker<void, Args...> {
 public:
	using ReturnValuesT = void;

	static void Invoke(Delegate<void(Args...)>& delegate, Args&&... args) {
		std::lock_guard<std::mutex> lock(delegate._mutex);

		if (delegate._functions.empty()) { return; }

		for (auto it = delegate._functions.begin(); it != delegate._functions.end();) {
			if (it->IsExpired()) {
				it = delegate._functions.erase(it);
				continue;
			}

			it->Function(std::forward<Args>(args)...);
			++it;
		}
	}
};

// Template class used to invoke delegates that can be cancelled. All delegate functions must return a bool, and if any
// delegate function returns true, the remaining delegate functions will not be evaluated.
template <typename... Args>
class CancellableInvoker {
 public:
	using ReturnValuesT = void;

	static void Invoke(CancellableDelegate<Args...>& delegate, Args&&... args) {
		std::lock_guard<std::mutex> lock(delegate._mutex);

		if (delegate._functions.empty()) { return; }

		for (auto it = delegate._functions.begin(); it != delegate._functions.end();) {
			if (it->IsExpired()) {
				it = delegate._functions.erase(it);
				continue;
			}

			if (it->Function(std::forward<Args>(args)...)) { break; }

			++it;
		}
	}
};

// Delegate object class. This object holds a dynamic number of callbacks that can be invoked using operator() on the
// Delegate object. Callbacks can be added and removed at will during runtime, and all valid callbacks at the time of
// invocation will be called.
template <typename ReturnT, typename... Args>
class Delegate<ReturnT(Args...)> {
 public:
	using FunctionT  = std::function<ReturnT(Args...)>;
	using InvokerT   = Invoker<ReturnT, Args...>;
	using ObserversT = std::vector<std::weak_ptr<bool>>;

	// A FunctionPair represents a single "callback" registered to this Delegate. It includes a function to call, and a
	// list of "observers". Observers can be any class that extends the Observer class. At the time of registering a
	// callback, one may optionally include a list of observers. If any observers have been destroyed before the callback
	// is called, the callback will automatically be removed.
	struct FunctionPair {
		FunctionT Function;
		ObserversT Observers;

		bool IsExpired() const {
			for (const auto& observer : Observers) {
				if (observer.expired()) { return true; }
			}

			return false;
		}
	};

	Delegate() = default;

	template <IsObserver... ObserverList>
	void Add(FunctionT&& function, ObserverList... observers) {
		ObserversT observerList;
		if constexpr (sizeof...(observers) != 0) {
			observerList.reserve(sizeof...(observers));
			for (const auto& observer : {observers...}) {
				observerList.emplace_back(static_cast<Observer*>(std::to_address(observer))->ObserverIsAlive);
			}
		}

		std::lock_guard<std::mutex> lock(_mutex);
		_functions.emplace_back(FunctionPair{std::move(function), std::move(observerList)});
	}

	void Clear() {
		std::lock_guard<std::mutex> lock(_mutex);
		_functions.clear();
	}

	typename InvokerT::ReturnValuesT Invoke(Args... args) {
		return InvokerT::Invoke(*this, std::forward<Args>(args)...);
	}

	void MoveFunctions(Delegate<ReturnT(Args...)>& from, const ObserversT& exclude = {}) {
		std::lock_guard<std::mutex> lock(_mutex);
		std::lock_guard<std::mutex> lock2(from._mutex);
		for (auto it = from._functions.begin(); it != from._functions.end();) {
			bool move = true;
			for (const auto& excluded : exclude) {
				auto excludePtr = excluded.lock();
				for (const auto& observer : it->Observers) {
					auto observerPtr = observer.lock();
					if (observerPtr.get() == excludePtr.get()) { move = false; }
				}
			}

			if (move) {
				std::move(from._functions.begin(), it, std::back_inserter(_functions));
				it = from._functions.erase(from._functions.begin(), it);
			} else {
				++it;
			}
		}
	}

	void Remove(const FunctionT&& function) {
		std::lock_guard<std::mutex> lock(_mutex);

		_functions.erase(std::remove_if(_functions.begin(),
		                                _functions.end(),
		                                [&function](const FunctionPair& f) { return Hash(f.Function) == Hash(function); }),
		                 _functions.end());
	}

	template <IsObserver... ObserverList>
	void RemoveObservers(ObserverList... observers) {
		ObserversT observerList;
		if constexpr (sizeof...(observers) != 0) {
			observerList.reserve(sizeof...(observers));
			for (const auto& observer : {observers...}) {
				observerList.emplace_back(static_cast<Observer*>(std::to_address(observer))->ObserverIsAlive);
			}
		}

		std::lock_guard<std::mutex> lock(_mutex);
		for (auto functionIt = _functions.begin(); functionIt != _functions.end();) {
			for (auto observerIt = functionIt->Observers.begin(); observerIt != functionIt->Observers.end();) {
				bool erase       = false;
				auto observerPtr = observerIt->lock();
				for (const auto& remove : observerList) {
					auto erasePtr = remove.lock();
					if (observerPtr.get() == erasePtr.get()) { erase = true; }
				}

				if (erase) {
					observerIt = functionIt->Observers.erase(observerIt);
				} else {
					++observerIt;
				}
			}
		}
	}

	Delegate<ReturnT(Args...)>& operator+=(FunctionT&& function) {
		Add(std::move(function));

		return *this;
	}

	Delegate<ReturnT(Args...)>& operator-=(FunctionT&& function) {
		Remove(std::move(function));

		return *this;
	}

	typename InvokerT::ReturnValuesT operator()(Args... args) {
		return InvokerT::Invoke(*this, std::forward<Args>(args)...);
	}

 private:
	friend InvokerT;

	static constexpr size_t Hash(const FunctionT& function) {
		return function.target_type().hash_code();
	}

	std::vector<FunctionPair> _functions;
	std::mutex _mutex;
};

// Specialized Delegate type that can be "cancelled". All delegate functions must return a bool. When invoking the
// delegate, callbacks are invoked in the order which they were registered. However, if any callback returns true,
// execution stops and subsequent callbacks will not be invoked.
template <typename... Args>
class CancellableDelegate {
 public:
	using ReturnT    = bool;
	using FunctionT  = std::function<ReturnT(Args...)>;
	using InvokerT   = CancellableInvoker<ReturnT, Args...>;
	using ObserversT = std::vector<std::weak_ptr<bool>>;

	// A FunctionPair represents a single "callback" registered to this Delegate. It includes a function to call, and a
	// list of "observers". Observers can be any class that extends the Observer class. At the time of registering a
	// callback, one may optionally include a list of observers. If any observers have been destroyed before the callback
	// is called, the callback will automatically be removed.
	struct FunctionPair {
		FunctionT Function;
		ObserversT Observers;

		bool IsExpired() const {
			for (const auto& observer : Observers) {
				if (observer.expired()) { return true; }
			}

			return false;
		}
	};

	CancellableDelegate() = default;

	template <IsObserver... ObserverList>
	void Add(FunctionT&& function, ObserverList... observers) {
		ObserversT observerList;
		if constexpr (sizeof...(observers) != 0) {
			observerList.reserve(sizeof...(observers));
			for (const auto& observer : {observers...}) {
				observerList.emplace_back(static_cast<Observer*>(std::to_address(observer))->ObserverIsAlive);
			}
		}

		std::lock_guard<std::mutex> lock(_mutex);
		_functions.emplace_back(FunctionPair{std::move(function), std::move(observerList)});
	}

	void Clear() {
		std::lock_guard<std::mutex> lock(_mutex);
		_functions.clear();
	}

	typename InvokerT::ReturnValuesT Invoke(Args&&... args) {
		return InvokerT::Invoke(*this, std::forward<Args>(args)...);
	}

	void MoveFunctions(Delegate<ReturnT(Args...)>& from, const ObserversT& exclude = {}) {
		std::lock_guard<std::mutex> lock(_mutex);
		std::lock_guard<std::mutex> lock2(from._mutex);
		for (auto it = from._functions.begin(); it != from._functions.end();) {
			bool move = true;
			for (const auto& excluded : exclude) {
				auto excludePtr = excluded.lock();
				for (const auto& observer : it->Observers) {
					auto observerPtr = observer.lock();
					if (observerPtr.get() == excludePtr.get()) { move = false; }
				}
			}

			if (move) {
				std::move(from._functions.begin(), it, std::back_inserter(_functions));
				it = from._functions.erase(from._functions.begin(), it);
			} else {
				++it;
			}
		}
	}

	void Remove(const FunctionT&& function) {
		std::lock_guard<std::mutex> lock(_mutex);

		_functions.erase(std::remove_if(_functions.begin(),
		                                _functions.end(),
		                                [&function](const FunctionPair& f) { return Hash(f.Function) == Hash(function); }),
		                 _functions.end());
	}

	template <IsObserver... ObserverList>
	void RemoveObservers(ObserverList... observers) {
		ObserversT observerList;
		if constexpr (sizeof...(observers) != 0) {
			observerList.reserve(sizeof...(observers));
			for (const auto& observer : {observers...}) {
				observerList.emplace_back(static_cast<Observer*>(std::to_address(observer))->ObserverIsAlive);
			}
		}

		std::lock_guard<std::mutex> lock(_mutex);
		for (auto functionIt = _functions.begin(); functionIt != _functions.end();) {
			for (auto observerIt = functionIt->Observers.begin(); observerIt != functionIt->Observers.end();) {
				bool erase       = false;
				auto observerPtr = observerIt->lock();
				for (const auto& remove : observerList) {
					auto erasePtr = remove.lock();
					if (observerPtr.get() == erasePtr.get()) { erase = true; }
				}

				if (erase) {
					observerIt = functionIt->Observers.erase(observerIt);
				} else {
					++observerIt;
				}
			}
		}
	}

	Delegate<ReturnT(Args...)>& operator+=(FunctionT&& function) {
		Add(std::move(function));

		return *this;
	}

	Delegate<ReturnT(Args...)>& operator-=(FunctionT&& function) {
		Remove(std::move(function));

		return *this;
	}

	typename InvokerT::ReturnValuesT operator()(Args&&... args) {
		return InvokerT::Invoke(*this, std::forward<Args>(args)...);
	}

 private:
	friend InvokerT;

	static constexpr size_t Hash(const FunctionT& function) {
		return function.target_type().hash_code();
	}

	std::vector<FunctionPair> _functions;
	std::mutex _mutex;
};

// Delegate values can have callbacks added just like normal delegates, but instead of being invoked on demand, they are
// invoked any time the value they contain changes.
template <typename T>
class DelegateValue : public Delegate<void(T&)> {
 public:
	DelegateValue()                                = default;
	DelegateValue(const DelegateValue&)            = delete;
	DelegateValue& operator=(const DelegateValue&) = delete;

	template <typename... Args>
	DelegateValue(Args&&... args) : _value(std::forward<Args>(args)...) {}

	DelegateValue& operator=(const T& value) {
		_value = value;
		Invoke(_value);

		return *this;
	}
	DelegateValue& operator=(T&& value) {
		_value = std::move(value);
		Invoke(_value);

		return *this;
	}

	operator const T&() const noexcept {
		return _value;
	}
	const T& Get() const {
		return _value;
	}
	const T& operator*() const {
		return _value;
	}
	const T* const operator->() const {
		return &_value;
	}

 protected:
	T _value = {};
};
}  // namespace Luna
