#pragma once

#include <Luna/Utility/NonCopyable.hpp>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

namespace Luna {
template <typename>
class Delegate;

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

// Template class used to invoke delegates that return some type of value. The invoker will look for any delegate
// callbacks that have expired observers and remove them from the delegate.
template <typename ReturnT, typename... Args>
class Invoker {
 public:
	using ReturnValuesT = std::vector<ReturnT>;

	static ReturnValuesT Invoke(Delegate<ReturnT(Args...)>& delegate, Args... args) {
		std::lock_guard<std::mutex> lock(delegate._mutex);

		ReturnValuesT values;
		for (auto it = delegate._functions.begin(); it != delegate._functions.end();) {
			if (it->IsExpired()) {
				it = delegate._functions.erase(it);
				continue;
			}

			values.emplace_back((*it->Function)(args...));
			++it;
		}

		return values;
	}
};

// Template class used to invoke delegates that have no return value. The invoker will look for any delegate callbacks
// that have expired observers and remove them from the delegate.
template <typename... Args>
class Invoker<void, Args...> {
 public:
	using ReturnValuesT = void;

	static void Invoke(Delegate<void(Args...)>& delegate, Args... args) {
		std::lock_guard<std::mutex> lock(delegate._mutex);

		if (delegate._functions.empty()) { return; }

		for (auto it = delegate._functions.begin(); it != delegate._functions.end();) {
			if (it->IsExpired()) {
				it = delegate._functions.erase(it);
				continue;
			}

			it->Function(args...);
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

	// A FunctionPair represents a single "callback" registered to this delegate. It includes a function to call, and a
	// list of "observers". Observers can be any class that extends the Observers class. At the time of registering a
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

	Delegate()                   = default;
	virtual ~Delegate() noexcept = default;

	template <typename... ObserverList>
	void Add(FunctionT&& function, ObserverList... observers) {
		std::lock_guard<std::mutex> lock(_mutex);

		ObserversT observerList;
		if constexpr (sizeof...(observers) != 0) {
			for (const auto& observer : {observers...}) {
				observerList.emplace_back(std::to_address(observer)->ObserverIsAlive);
			}
		}

		_functions.emplace_back(FunctionPair{std::move(function), observerList});
	}

	void Remove(const FunctionT&& function) {
		std::lock_guard<std::mutex> lock(_mutex);

		_functions.erase(std::remove_if(_functions.begin(),
		                                _functions.end(),
		                                [function](FunctionPair& f) { return Hash(f.Function) == Hash(function); }),
		                 _functions.end());
	}

	template <typename... ObserverList>
	void RemoveObservers(ObserverList... observers) {
		ObserversT removes;

		if constexpr (sizeof...(observers) != 0) {
			for (const auto& observer : {observers...}) { removes.emplace_back(std::to_address(observer)->ObserverIsAlive); }
		}

		for (auto it = _functions.begin(); it != _functions.end();) {
			for (auto it1 = it->Observers.begin(); it1 != it->Observers.end();) {
				bool erase = false;
				auto opt   = it1->lock();
				for (const auto& remove : removes) {
					auto ept = remove.lock();
					if (opt.get() == ept.get()) { erase = true; }
				}
				if (erase) {
					it1 = it->Observers.erase(it1);
				} else {
					++it1;
				}
			}

			if (it->Observers.empty()) {
				it = _functions.erase(it);
			} else {
				++it;
			}
		}
	}

	void MoveFunctions(Delegate& from, const ObserversT& exclude = {}) {
		for (auto it = from._functions.begin(); it < from._functions.end();) {
			bool move = true;
			for (const auto& excluded : exclude) {
				auto ept = excluded.lock();
				for (const auto& observer : it->Observers) {
					auto opt = observer.lock();
					if (opt.get() == ept.get()) { move = false; }
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

	void Clear() {
		std::lock_guard<std::mutex> lock(_mutex);

		_functions.clear();
	}

	typename InvokerT::ReturnValuesT Invoke(Args... args) {
		return InvokerT::Invoke(*this, args...);
	}

	Delegate& operator+=(FunctionT&& function) {
		return Add(std::move(function));
	}

	Delegate& operator-=(FunctionT&& function) {
		return Remove(std::move(function));
	}

	typename InvokerT::ReturnValuesT operator()(Args... args) {
		return InvokerT::Invoke(*this, args...);
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
class DelegateValue : public Delegate<void(T)>, NonCopyable {
 public:
	template <typename... Args>
	DelegateValue(Args... args) : _value(std::forward<Args>(args)...) {}
	virtual ~DelegateValue() noexcept = default;

	DelegateValue& operator=(T value) {
		_value = value;
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
	const T* operator->() const {
		return &_value;
	}

 protected:
	T _value;
};
}  // namespace Luna
