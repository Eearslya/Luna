#pragma once

#include <filesystem>

#include "Assets/Mesh.hpp"
#include "Utility/IntrusivePtr.hpp"

namespace Luna {
struct MeshComponent {
	MeshComponent()                     = default;
	MeshComponent(const MeshComponent&) = default;

	IntrusivePtr<Mesh> Mesh;
	std::filesystem::path MeshAssetPath;
	uint32_t SubmeshIndex = 0;
};
}  // namespace Luna
