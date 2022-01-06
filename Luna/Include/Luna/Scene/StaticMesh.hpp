#pragma once

#include <imgui.h>

#include <Luna/Assets/Material.hpp>
#include <Luna/Vulkan/Common.hpp>
#include <entt/entt.hpp>
#include <glm/glm.hpp>

namespace Luna {
constexpr static const int MaxBones = 128;

struct SubMesh {
	uint32_t FirstVertex = 0;
	uint32_t FirstIndex  = 0;
	uint32_t IndexCount  = 0;
	uint32_t Material    = 0;
};

struct MeshData {
	glm::mat4 Transform;
	std::array<glm::mat4, MaxBones> BoneMatrices;
	float BoneCount = 0.0f;
};

struct StaticMesh {
	// std::vector<Material> Materials;
	std::vector<SubMesh> SubMeshes;
	Vulkan::BufferHandle PositionBuffer;
	Vulkan::BufferHandle NormalBuffer;
	Vulkan::BufferHandle TexcoordBuffer;
	Vulkan::BufferHandle IndexBuffer;

	uint64_t VertexCount    = 0;
	uint64_t IndexCount     = 0;
	vk::DeviceSize ByteSize = 0;

	void DrawComponent(const entt::registry& registry) {
		if (ImGui::CollapsingHeader("StaticMesh##StaticMesh", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::Text("Vertices: %lld", VertexCount);
			ImGui::Text("Indices: %lld", IndexCount);
			ImGui::Text("Submeshes: %lld", SubMeshes.size());
			// ImGui::Text("Materials: %lld", Materials.size());
			ImGui::Text("Size: %s", Vulkan::FormatSize(ByteSize).c_str());
		}
	}
};
}  // namespace Luna
