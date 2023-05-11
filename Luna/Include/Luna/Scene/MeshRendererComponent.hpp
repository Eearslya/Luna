#pragma once

#include <Luna/Assets/Asset.hpp>
#include <Luna/Scene/Component.hpp>

namespace Luna {
struct MeshRendererComponent final : public Component {
	AssetHandle MeshAsset = 0;
	std::vector<AssetHandle> MaterialAssets;

	virtual bool Deserialize(const nlohmann::json& data) override {
		MeshAsset = data.at("MeshAsset").get<uint64_t>();

		MaterialAssets.clear();
		if (data.contains("Materials")) {
			const auto materials = data["Materials"];
			if (materials.is_array()) {
				MaterialAssets.reserve(materials.size());
				for (const auto& mat : materials) { MaterialAssets.push_back(mat.get<uint64_t>()); }
			}
		}

		return true;
	}

	virtual void Serialize(nlohmann::json& data) const override {
		data["MeshAsset"] = uint64_t(MeshAsset);
		auto materials    = nlohmann::json::array();
		for (const auto& asset : MaterialAssets) { materials.push_back(uint64_t(asset)); }
		data["Materials"] = materials;
	}
};
}  // namespace Luna
