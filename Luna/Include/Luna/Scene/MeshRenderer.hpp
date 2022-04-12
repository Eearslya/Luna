#pragma once

#include <imgui.h>

#include <Luna/Assets/Material.hpp>
#include <Luna/Assets/StaticMesh.hpp>
#include <Luna/Assets/Texture.hpp>
#include <glm/glm.hpp>

namespace Luna {
struct MeshRenderer {
	StaticMeshHandle Mesh;
	std::vector<MaterialHandle> Materials;

	void DrawComponent(entt::registry& registry) {
		if (ImGui::CollapsingHeader("MeshRenderer##MeshRenderer", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImDrawList* drawList   = ImGui::GetWindowDrawList();
			const auto ShowTexture = [&](const TextureHandle& texture, const std::string& label) -> void {
				if (texture && texture->Ready) {
					ImGui::Image(reinterpret_cast<ImTextureID>(const_cast<Vulkan::ImageView*>(texture->Image->GetView().Get())),
					             ImVec2(100.0f, 100.0f));
					if (ImGui::IsItemHovered()) {
						ImGui::BeginTooltip();
						ImGui::Image(reinterpret_cast<ImTextureID>(const_cast<Vulkan::ImageView*>(texture->Image->GetView().Get())),
						             ImVec2(512.0f, 512.0f));
						ImGui::EndTooltip();
					}
				} else {
					const ImVec4 colorFloat(0.2f, 0.2, 0.2f, 1.0f);
					const ImColor color(colorFloat);
					const auto p = ImGui::GetCursorScreenPos();
					drawList->AddRectFilled(p, ImVec2(p.x + 100.0f, p.y + 100.0f), color);
					ImGui::Dummy(ImVec2(100.0f, 100.0f));
				}
				ImGui::SameLine();
				ImGui::BeginGroup();
				ImGui::Text("%s", label.c_str());
				if (texture) {
					if (texture->Ready) {
						const auto& info = texture->Image->GetCreateInfo();
						ImGui::Text("%s - %u x %u", vk::to_string(info.Format).c_str(), info.Extent.width, info.Extent.height);
						ImGui::Text("%s", Vulkan::FormatSize(texture->Image->GetImageSize()).c_str());
						ImGui::Text("%u Mip Level%s", info.MipLevels, info.MipLevels > 1 ? "s" : "");
					} else {
						ImGui::Text("Processing...");
					}
				} else {
					ImGui::Text("No Texture Assigned");
				}
				ImGui::EndGroup();
			};

			ImGui::Text("Mesh: %s", Mesh->Ready ? "Ready" : "Loading");
			ImGui::Text("Vertices: %llu", Mesh->TotalVertexCount);
			ImGui::Text("Triangles: %llu", Mesh->TotalVertexCount / 3llu);
			ImGui::Text("Submeshes: %llu", Mesh->SubMeshes.size());
			ImGui::Separator();
			for (size_t i = 0; i < Materials.size(); ++i) {
				auto& material               = Materials[i];
				const std::string identifier = "Material: " + material->Name + "##Material_" + std::to_string(i);
				if (ImGui::CollapsingHeader(identifier.c_str())) {
					ImGui::ColorEdit4("Base Color Factor", glm::value_ptr(material->BaseColorFactor));
					ShowTexture(material->Albedo, "Albedo");
					ShowTexture(material->Normal, "Normal");
					ImGui::DragFloat("Metallic", &material->MetallicFactor, 0.01f, 0.0f, 1.0f);
					ImGui::DragFloat("Roughness", &material->RoughnessFactor, 0.01f, 0.0f, 1.0f);
					ShowTexture(material->PBR, "Physical Descriptor");
					ImGui::ColorEdit3("Emissive Factor", glm::value_ptr(material->EmissiveFactor));
					ShowTexture(material->Emissive, "Emissive");
					const char* blendModes[] = {"Opaque", "Mask", "Blend"};
					ImGui::Combo("Blend Mode", reinterpret_cast<int*>(&material->BlendMode), blendModes, 3);
					ImGui::DragFloat("Alpha Cutoff", &material->AlphaCutoff, 0.01f, 0.0f, 1.0f);
				}
			}
		}
	}
};
}  // namespace Luna
