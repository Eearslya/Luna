#pragma once

#include <string>

namespace Luna {
class App {
	friend class Engine;

 public:
	explicit App(const std::string& name) : _name(name) {}
	virtual ~App() noexcept = default;

	virtual void Start()  = 0;
	virtual void Stop()   = 0;
	virtual void Update() = 0;

	const std::string& GetName() const {
		return _name;
	}

	void SetName(const std::string& name) {
		_name = name;
	}

 private:
	std::string _name;
	bool _started = false;
};
}  // namespace Luna
