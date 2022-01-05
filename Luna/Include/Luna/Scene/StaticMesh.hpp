#pragma once

#include <imgui.h>

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

struct MaterialData {
	glm::vec4 BaseColorFactor;
	glm::vec4 EmissiveFactor;
	int HasAlbedo     = 0;
	int HasNormal     = 0;
	int HasPBR        = 0;
	int HasEmissive   = 0;
	float AlphaMask   = 0.0f;
	float AlphaCutoff = 0.0f;
	float Metallic    = 0.0f;
	float Roughness   = 0.0f;
};

struct Texture {
	Vulkan::ImageHandle Image;
	Vulkan::Sampler* Sampler = nullptr;
};

struct Material {
	Texture Albedo;
	Texture Normal;
	Texture PBR;
	bool DualSided = false;
	Vulkan::BufferHandle Data;
};

struct MeshData {
	glm::mat4 Transform;
	std::array<glm::mat4, MaxBones> BoneMatrices;
	float BoneCount = 0.0f;
};

struct StaticMesh {
	std::vector<Material> Materials;
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
			ImGui::Text("Materials: %lld", Materials.size());
			ImGui::Text("Size: %s", Vulkan::FormatSize(ByteSize).c_str());
		}
	}
};
}  // namespace Luna
