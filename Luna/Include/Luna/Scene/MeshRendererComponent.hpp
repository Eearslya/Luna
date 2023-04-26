#pragma once

#include <Luna/Assets/Asset.hpp>
#include <Luna/Scene/Component.hpp>

namespace Luna {
struct MeshRendererComponent final : public Component {
	AssetHandle MeshAsset = 0;

	virtual bool Deserialize(const nlohmann::json& data) override {
		MeshAsset = data.at("MeshAsset").get<uint64_t>();

		return true;
	}

	virtual void Serialize(nlohmann::json& data) const override {
		data["MeshAsset"] = uint64_t(MeshAsset);
	}
};
}  // namespace Luna
