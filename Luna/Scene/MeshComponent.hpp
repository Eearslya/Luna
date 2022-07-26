#pragma once

#include <filesystem>
#include <vector>

#include "Assets/Material.hpp"
#include "Assets/Mesh.hpp"
#include "Utility/AABB.hpp"
#include "Utility/IntrusivePtr.hpp"

namespace Luna {
struct MeshComponent {
	MeshComponent()                     = default;
	MeshComponent(const MeshComponent&) = default;

	AABB Bounds;
	IntrusivePtr<Mesh> Mesh;
	std::vector<MaterialHandle> Materials;

	// Legacy
	std::filesystem::path MeshAssetPath;
	uint32_t SubmeshIndex = 0;
};
}  // namespace Luna
