#pragma once

#include <string>

namespace Luna {
struct NameComponent {
	NameComponent() = default;
	NameComponent(const std::string& name) : Name(name) {}
	NameComponent(const NameComponent&) = default;

	std::string Name;
};
}  // namespace Luna
