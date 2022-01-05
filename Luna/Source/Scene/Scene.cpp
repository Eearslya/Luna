#include <imgui.h>
#include <stb_image.h>
#include <tiny_gltf.h>

#include <Luna/Core/Log.hpp>
#include <Luna/Filesystem/Filesystem.hpp>
#include <Luna/Graphics/Graphics.hpp>
#include <Luna/Scene/Scene.hpp>
#include <Luna/Scene/StaticMesh.hpp>
#include <Luna/Scene/TransformComponent.hpp>
#include <Luna/Vulkan/Buffer.hpp>
#include <Luna/Vulkan/Device.hpp>
#include <Luna/Vulkan/Image.hpp>
#include <Luna/Vulkan/Sampler.hpp>
#include <filesystem>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>

namespace Luna {
Scene::Scene() {
	_root               = _registry.create();
	auto& rootTransform = _registry.emplace<TransformComponent>(_root);
	rootTransform.Name  = "Root";
}

Scene::~Scene() noexcept {}

entt::entity Scene::CreateEntity(const std::string& name, std::optional<entt::entity> parent) {
	entt::entity realParent = parent.has_value() ? parent.value() : _root;

	entt::entity e   = _registry.create();
	auto& transform  = _registry.emplace<TransformComponent>(e);
	transform.Parent = realParent;
	transform.Name   = name;

	if (_registry.valid(realParent)) {
		auto& parentTransform = _registry.get<TransformComponent>(realParent);
		parentTransform.Children.push_back(e);
	}

	return e;
}

void Scene::LoadModel(const std::string& filePath, entt::entity parent) {
	auto graphics = Graphics::Get();
	auto& device  = graphics->GetDevice();
	const std::filesystem::path path(filePath);
	const auto fileName = path.filename().string();

	// Load glTF model.
	tinygltf::Model model;
	{
		tinygltf::TinyGLTF loader;
		tinygltf::FsCallbacks fsCallbacks;
		fsCallbacks.FileExists = [](const std::string& filename, void* userData) -> bool {
			return Filesystem::Get()->Exists(filename);
		};
		fsCallbacks.ExpandFilePath = [](const std::string& path, void* userData) -> std::string { return path; };
		fsCallbacks.ReadWholeFile =
			[](std::vector<unsigned char>* bytes, std::string* err, const std::string& path, void* userData) -> bool {
			auto fs = Filesystem::Get();
			if (!fs->Exists(path)) {
				*err = "File not found";
				return false;
			}

			auto data = fs->ReadBytes(path);
			if (data.has_value()) {
				*bytes = data.value();
				return true;
			} else {
				*err = "Failed to open file for reading";
				return false;
			}
		};
		fsCallbacks.WriteWholeFile =
			[](
				std::string* err, const std::string& path, const std::vector<unsigned char>& contents, void* userData) -> bool {
			*err = "Write not supported";
			return false;
		};
		loader.SetFsCallbacks(fsCallbacks);
		std::string gltfError;
		std::string gltfWarning;

		ElapsedTime gltfLoadTime;
		const bool loaded = loader.LoadASCIIFromFile(&model, &gltfError, &gltfWarning, filePath);
		gltfLoadTime.Update();
		if (!gltfError.empty()) { Log::Error("[Graphics] glTF Error: {}", gltfError); }
		if (!gltfWarning.empty()) { Log::Warning("[Graphics] glTF Warning: {}", gltfWarning); }
		if (loaded) {
			Log::Info("[Graphics] {} loaded in {}ms", fileName, gltfLoadTime.Get().Milliseconds());
		} else {
			Log::Error("[Graphics] Failed to load glTF model!");
			return;
		}
	}

	// Create our root entity.
	auto rootEntity = CreateEntity(fileName, parent);

	// Parse model for meshes.
	{
		Log::Trace("[Graphics] Parsing glTF file.");

		const int defaultScene = model.defaultScene == -1 ? 0 : model.defaultScene;
		Log::Trace("[Graphics] - Loading glTF scene {}.", defaultScene);
		const auto& scene = model.scenes[defaultScene];

		std::vector<Material> materials;
		Log::Trace("[Graphics] - Scene contains {} materials.", model.materials.size());
		for (const auto& gltfMaterial : model.materials) {
			auto& material = materials.emplace_back();

			material.DualSided = gltfMaterial.doubleSided;

			MaterialData data{};
			data.AlphaMask       = gltfMaterial.alphaMode.compare("MASK") == 0 ? 1.0f : 0.0f;
			data.AlphaCutoff     = gltfMaterial.alphaCutoff;
			data.BaseColorFactor = glm::make_vec4(gltfMaterial.pbrMetallicRoughness.baseColorFactor.data());
			data.EmissiveFactor  = glm::make_vec4(gltfMaterial.emissiveFactor.data());
			data.Metallic        = gltfMaterial.pbrMetallicRoughness.metallicFactor;
			data.Roughness       = gltfMaterial.pbrMetallicRoughness.roughnessFactor;

			const auto LoadTexture = [&](size_t textureID, bool color, Texture& texture) -> void {
				const auto& gltfTexture     = model.textures[textureID];
				const auto& gltfImage       = model.images[gltfTexture.source];
				const unsigned char* pixels = gltfImage.image.data();
				if (pixels) {
					const Vulkan::InitialImageData initialImage{.Data = pixels};
					vk::Format format = vk::Format::eUndefined;
					if (color) {
						format = gltfImage.component == 4 ? vk::Format::eR8G8B8A8Srgb : vk::Format::eR8G8B8Srgb;
					} else {
						format = gltfImage.component == 4 ? vk::Format::eR8G8B8A8Unorm : vk::Format::eR8G8B8Unorm;
					}
					texture.Image = device.CreateImage(
						Vulkan::ImageCreateInfo::Immutable2D(format, vk::Extent2D(gltfImage.width, gltfImage.height), true),
						&initialImage);
				} else {
					Log::Error("[Graphics] Failed to load texture: {}", stbi_failure_reason());
				}

				if (gltfTexture.sampler >= 0) {
					const auto& gltfSampler = model.samplers[gltfTexture.sampler];
					Vulkan::SamplerCreateInfo sampler{.MaxLod = 11.0f};
					const auto& gpuInfo = device.GetGPUInfo();
					if (gpuInfo.EnabledFeatures.Features.samplerAnisotropy) {
						sampler.AnisotropyEnable = VK_TRUE;
						sampler.MaxAnisotropy    = std::min(gpuInfo.Properties.Properties.limits.maxSamplerAnisotropy, 8.0f);
					}
					switch (gltfSampler.magFilter) {
						case 9728:  // NEAREST
							sampler.MagFilter = vk::Filter::eNearest;
							break;
						case 9729:  // LINEAR
							sampler.MagFilter = vk::Filter::eLinear;
							break;
					}
					switch (gltfSampler.minFilter) {
						case 9728:  // NEAREST
							sampler.MinFilter = vk::Filter::eNearest;
							break;
						case 9729:  // LINEAR
							sampler.MinFilter = vk::Filter::eLinear;
							break;
						case 9984:  // NEAREST_MIPMAP_NEAREST
							sampler.MinFilter  = vk::Filter::eNearest;
							sampler.MipmapMode = vk::SamplerMipmapMode::eNearest;
							break;
						case 9985:  // LINEAR_MIPMAP_NEAREST
							sampler.MinFilter  = vk::Filter::eLinear;
							sampler.MipmapMode = vk::SamplerMipmapMode::eNearest;
							break;
						case 9986:  // NEAREST_MIPMAP_LINEAR
							sampler.MinFilter  = vk::Filter::eNearest;
							sampler.MipmapMode = vk::SamplerMipmapMode::eLinear;
							break;
						case 9987:  // LINEAR_MIPMAP_LINEAR
							sampler.MinFilter  = vk::Filter::eLinear;
							sampler.MipmapMode = vk::SamplerMipmapMode::eLinear;
							break;
					}
					switch (gltfSampler.wrapS) {
						case 33071:  // CLAMP_TO_EDGE
							sampler.AddressModeU = vk::SamplerAddressMode::eClampToEdge;
							break;
						case 33648:  // MIRRORED_REPEAT
							sampler.AddressModeU = vk::SamplerAddressMode::eMirroredRepeat;
							break;
						case 10497:  // REPEAT
							sampler.AddressModeU = vk::SamplerAddressMode::eRepeat;
							break;
					}
					switch (gltfSampler.wrapT) {
						case 33071:  // CLAMP_TO_EDGE
							sampler.AddressModeV = vk::SamplerAddressMode::eClampToEdge;
							break;
						case 33648:  // MIRRORED_REPEAT
							sampler.AddressModeV = vk::SamplerAddressMode::eMirroredRepeat;
							break;
						case 10497:  // REPEAT
							sampler.AddressModeV = vk::SamplerAddressMode::eRepeat;
							break;
					}
					texture.Sampler = device.RequestSampler(sampler);
				}
			};

			if (gltfMaterial.pbrMetallicRoughness.baseColorTexture.index >= 0) {
				LoadTexture(gltfMaterial.pbrMetallicRoughness.baseColorTexture.index, true, material.Albedo);
				data.HasAlbedo = 1;
			}
			if (gltfMaterial.pbrMetallicRoughness.metallicRoughnessTexture.index >= 0) {
				LoadTexture(gltfMaterial.pbrMetallicRoughness.metallicRoughnessTexture.index, false, material.PBR);
				data.HasPBR = 1;
			}
			if (gltfMaterial.normalTexture.index >= 0) {
				LoadTexture(gltfMaterial.normalTexture.index, false, material.Normal);
				data.HasNormal = 1;
			}

			material.Data = device.CreateBuffer(
				Vulkan::BufferCreateInfo(Vulkan::BufferDomain::Host, sizeof(data), vk::BufferUsageFlagBits::eUniformBuffer),
				&data);
		}

		Log::Trace("[Graphics] - Scene contains {} nodes.", scene.nodes.size());
		const std::function<void(int, entt::entity)> ParseNode = [&](int nodeID, entt::entity parentEntity) -> void {
			Log::Trace("[Graphics] - Parsing node {}.", nodeID);
			const auto& gltfNode    = model.nodes[nodeID];
			entt::entity nodeEntity = CreateEntity(gltfNode.name.size() > 0 ? gltfNode.name : "Node", parentEntity);

			auto& nodeTransform = _registry.get<TransformComponent>(nodeEntity);
			if (gltfNode.matrix.size() > 0) {
				const glm::mat4 matrix = glm::make_mat4(gltfNode.matrix.data());
				glm::vec3 scale;
				glm::quat rotation;
				glm::vec3 translation;
				glm::vec3 skew;
				glm::vec4 perspective;
				glm::decompose(matrix, scale, rotation, translation, skew, perspective);
				nodeTransform.Position = translation;
				nodeTransform.Rotation = glm::eulerAngles(rotation);
				nodeTransform.Scale    = scale;
			} else {
				if (gltfNode.translation.size() > 0) { nodeTransform.Position = glm::make_vec3(gltfNode.translation.data()); }
				if (gltfNode.rotation.size() > 0) {
					nodeTransform.Rotation = glm::eulerAngles(glm::make_quat(gltfNode.rotation.data()));
				}
				if (gltfNode.scale.size() > 0) { nodeTransform.Scale = glm::make_vec3(gltfNode.scale.data()); }
			}
			nodeTransform.UpdateGlobalTransform(_registry);

			if (gltfNode.mesh >= 0) {
				Log::Trace("[Graphics]   - Node has mesh {}.", gltfNode.mesh);
				const auto& gltfMesh = model.meshes[gltfNode.mesh];
				StaticMesh& mesh     = _registry.emplace<StaticMesh>(nodeEntity);
				mesh.Materials       = materials;

				std::vector<glm::vec3> positions;
				std::vector<glm::vec3> normals;
				std::vector<glm::vec2> texcoords;
				std::vector<uint32_t> indices;

				Log::Trace("[Graphics]   - Mesh has {} primitives.", gltfMesh.primitives.size());
				for (const auto& gltfPrimitive : gltfMesh.primitives) {
					if (gltfPrimitive.mode != 4) {
						Log::Error("[Graphics] Unsupported primitive mode {}.", gltfPrimitive.mode);
						continue;
					}

					auto& submesh       = mesh.SubMeshes.emplace_back();
					submesh.FirstVertex = positions.size();
					submesh.FirstIndex  = indices.size();
					submesh.Material    = gltfPrimitive.material;

					for (const auto& [attributeType, attributeAccessorID] : gltfPrimitive.attributes) {
						const auto& gltfAccessor   = model.accessors[attributeAccessorID];
						const auto& gltfBufferView = model.bufferViews[gltfAccessor.bufferView];
						const auto& gltfBuffer     = model.buffers[gltfBufferView.buffer];
						const unsigned char* data  = &gltfBuffer.data[gltfBufferView.byteOffset + gltfAccessor.byteOffset];
						const size_t dataStride    = gltfAccessor.ByteStride(gltfBufferView);
						const size_t dataSize      = dataStride * gltfAccessor.count;

						if (attributeType.compare("POSITION") == 0) {
							const glm::vec3* posData = reinterpret_cast<const glm::vec3*>(data);
							positions.insert(positions.end(), posData, posData + gltfAccessor.count);
						} else if (attributeType.compare("NORMAL") == 0) {
							const glm::vec3* norData = reinterpret_cast<const glm::vec3*>(data);
							normals.insert(normals.end(), norData, norData + gltfAccessor.count);
						} else if (attributeType.compare("TEXCOORD_0") == 0) {
							const glm::vec2* tc0Data = reinterpret_cast<const glm::vec2*>(data);
							texcoords.insert(texcoords.end(), tc0Data, tc0Data + gltfAccessor.count);
						}
					}

					if (gltfPrimitive.indices >= 0) {
						const auto& gltfAccessor   = model.accessors[gltfPrimitive.indices];
						const auto& gltfBufferView = model.bufferViews[gltfAccessor.bufferView];
						const auto& gltfBuffer     = model.buffers[gltfBufferView.buffer];
						const unsigned char* data  = &gltfBuffer.data.at(gltfBufferView.byteOffset + gltfAccessor.byteOffset);
						const size_t dataStride    = gltfAccessor.ByteStride(gltfBufferView);
						const size_t dataSize      = dataStride * gltfAccessor.count;

						submesh.IndexCount = gltfAccessor.count;
						indices.reserve(indices.size() + gltfAccessor.count);
						if (dataStride == 2) {
							const uint16_t* indData = reinterpret_cast<const uint16_t*>(data);
							for (size_t i = 0; i < gltfAccessor.count; ++i) { indices.push_back(static_cast<uint32_t>(indData[i])); }
						} else if (dataStride == 4) {
							const uint32_t* indData = reinterpret_cast<const uint32_t*>(data);
							indices.insert(indices.end(), indData, indData + gltfAccessor.count);
						}
					}

					const vk::DeviceSize positionSize = positions.size() * sizeof(glm::vec3);
					const vk::DeviceSize normalSize   = normals.size() * sizeof(glm::vec3);
					const vk::DeviceSize texcoordSize = texcoords.size() * sizeof(glm::vec2);
					const vk::DeviceSize indexSize    = indices.size() * sizeof(uint32_t);

					mesh.PositionBuffer =
						device.CreateBuffer(Vulkan::BufferCreateInfo(
																	Vulkan::BufferDomain::Device, positionSize, vk::BufferUsageFlagBits::eVertexBuffer),
					                      positions.data());
					mesh.NormalBuffer = device.CreateBuffer(
						Vulkan::BufferCreateInfo(Vulkan::BufferDomain::Device, normalSize, vk::BufferUsageFlagBits::eVertexBuffer),
						normals.data());
					mesh.TexcoordBuffer =
						device.CreateBuffer(Vulkan::BufferCreateInfo(
																	Vulkan::BufferDomain::Device, texcoordSize, vk::BufferUsageFlagBits::eVertexBuffer),
					                      texcoords.data());
					mesh.IndexBuffer = device.CreateBuffer(
						Vulkan::BufferCreateInfo(Vulkan::BufferDomain::Device, indexSize, vk::BufferUsageFlagBits::eIndexBuffer),
						indices.data());

					mesh.VertexCount = positions.size();
					mesh.IndexCount  = indices.size();
					mesh.ByteSize    = positionSize + normalSize + texcoordSize + indexSize;
				}
			}

			for (const auto& childID : gltfNode.children) { ParseNode(childID, nodeEntity); }
		};
		for (const auto& nodeID : scene.nodes) { ParseNode(nodeID, rootEntity); }
	}
}

void Scene::DrawSceneGraph() {
	if (ImGui::Begin("Scene")) {
		if (ImGui::BeginTable("SceneGraph", 2, ImGuiTableFlags_BordersInnerV)) {
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::BeginGroup();
			std::function<void(const entt::entity)> DisplayEntity = [&](const entt::entity entity) -> void {
				const auto& transform       = _registry.get<TransformComponent>(entity);
				const bool hasChildren      = transform.Children.size() > 0;
				ImGuiTreeNodeFlags addFlags = entity == _selected ? ImGuiTreeNodeFlags_Selected : 0;
				if (hasChildren) {
					bool open = ImGui::TreeNodeEx(transform.Name.c_str(), ImGuiTreeNodeFlags_OpenOnArrow | addFlags);
					if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) { _selected = entity; }
					if (open) {
						for (const auto child : transform.Children) { DisplayEntity(child); }
						ImGui::TreePop();
					}
				} else {
					ImGui::TreeNodeEx(
						transform.Name.c_str(),
						ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_Bullet | ImGuiTreeNodeFlags_NoTreePushOnOpen | addFlags);
					if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) { _selected = entity; }
				}
			};
			DisplayEntity(_root);
			ImGui::EndGroup();

			ImGui::TableNextColumn();
			if (_registry.valid(_selected)) {
				if (auto* comp = _registry.try_get<TransformComponent>(_selected)) { comp->DrawComponent(_registry); }
				if (auto* comp = _registry.try_get<StaticMesh>(_selected)) { comp->DrawComponent(_registry); }
			}

			ImGui::EndTable();
		}

		ImGui::End();
	}
}
}  // namespace Luna
