#pragma once

#include <Luna/Scene/Component.hpp>
#include <Luna/Utility/UUID.hpp>

namespace Luna {
struct IdComponent : public Component {
 public:
	IdComponent() = default;
	IdComponent(const UUID& uuid) : Id(uuid) {}

	UUID Id;

	bool Deserialize(const nlohmann::json& data) override {
		Id = data.at("Id").get<uint64_t>();

		return true;
	}

	void Serialize(nlohmann::json& data) const override {
		data["Id"] = uint64_t(Id);
	}
};
}  // namespace Luna
