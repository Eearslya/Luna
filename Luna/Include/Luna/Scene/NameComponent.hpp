#pragma once

#include <Luna/Scene/Component.hpp>
#include <string>

namespace Luna {
struct NameComponent final : public Component {
	NameComponent() = default;
	NameComponent(const std::string& name) : Name(name) {}
	NameComponent(const NameComponent&) = default;

	std::string Name;

	bool Deserialize(const nlohmann::json& data) override {
		Name = data.at("Name").get<std::string>();

		return true;
	}

	void Serialize(nlohmann::json& data) const override {
		data["Name"] = Name;
	}
};
}  // namespace Luna
