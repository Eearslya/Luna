#pragma once

#include <filesystem>

namespace Luna {
struct MeshComponent {
	MeshComponent()                     = default;
	MeshComponent(const MeshComponent&) = default;

	std::filesystem::path MeshAssetPath;
	uint32_t SubmeshIndex = 0;
};
}  // namespace Luna
