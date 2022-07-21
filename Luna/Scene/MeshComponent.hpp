#pragma once

#include <filesystem>

#include "Utility/IntrusivePtr.hpp"

namespace Luna {
struct Mesh;

struct MeshComponent {
	MeshComponent()                     = default;
	MeshComponent(const MeshComponent&) = default;

	IntrusivePtr<Mesh> Mesh;
	std::filesystem::path MeshAssetPath;
	uint32_t SubmeshIndex = 0;
};
}  // namespace Luna
