#include <stb_image.h>
#include <tiny_gltf.h>

#include <Luna/Core/Log.hpp>
#include <Luna/Graphics/Graphics.hpp>
#include <Luna/Vulkan/Buffer.hpp>
#include <Luna/Vulkan/CommandBuffer.hpp>
#include <Luna/Vulkan/Context.hpp>
#include <Luna/Vulkan/Device.hpp>
#include <Luna/Vulkan/Fence.hpp>
#include <Luna/Vulkan/Image.hpp>
#include <Luna/Vulkan/RenderPass.hpp>
#include <Luna/Vulkan/Shader.hpp>
#include <Luna/Vulkan/Swapchain.hpp>
#include <filesystem>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace Luna {
Graphics::Graphics() {
	auto filesystem = Filesystem::Get();

	filesystem->AddSearchPath("Assets");

	const auto instanceExtensions                   = Window::Get()->GetRequiredInstanceExtensions();
	const std::vector<const char*> deviceExtensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
	_context   = std::make_unique<Vulkan::Context>(instanceExtensions, deviceExtensions);
	_device    = std::make_unique<Vulkan::Device>(*_context);
	_swapchain = std::make_unique<Vulkan::Swapchain>(*_device);

	// Load glTF model.
	const std::string gltfFile = "Models/Sponza/Sponza.gltf";
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
		const bool loaded = loader.LoadASCIIFromFile(&model, &gltfError, &gltfWarning, gltfFile);
		gltfLoadTime.Update();
		if (!gltfError.empty()) { Log::Error("[Graphics] glTF Error: {}", gltfError); }
		if (!gltfWarning.empty()) { Log::Warning("[Graphics] glTF Warning: {}", gltfWarning); }
		if (loaded) {
			Log::Info("[Graphics] {} loaded in {}ms", gltfFile, gltfLoadTime.Get().Milliseconds());
		} else {
			Log::Error("[Graphics] Failed to load glTF model!");
		}
	}

	// Parse model for meshes.
	std::vector<glm::vec3> positions;
	std::vector<glm::vec3> normals;
	std::vector<glm::vec2> texcoords;
	std::vector<uint16_t> indices;
	{
		Log::Trace("[Graphics] Parsing glTF file.");

		const int defaultScene = model.defaultScene == -1 ? 0 : model.defaultScene;
		Log::Trace("[Graphics] - Loading glTF scene {}.", defaultScene);
		const auto& scene = model.scenes[defaultScene];

		Log::Trace("[Graphics] - Scene contains {} nodes.", scene.nodes.size());
		const std::function<void(int)> ParseNode = [&](int nodeID) -> void {
			Log::Trace("[Graphics] - Parsing node {}.", nodeID);
			const auto& gltfNode = model.nodes[nodeID];

			if (gltfNode.mesh >= 0) {
				Log::Trace("[Graphics]   - Node has mesh {}.", gltfNode.mesh);
				const auto& gltfMesh = model.meshes[gltfNode.mesh];

				Log::Trace("[Graphics]   - Mesh has {} primitives.", gltfMesh.primitives.size());
				for (const auto& gltfPrimitive : gltfMesh.primitives) {
					if (gltfPrimitive.mode != 4) {
						Log::Error("[Graphics] Unsupported primitive mode {}.", gltfPrimitive.mode);
						continue;
					}

					auto& submesh       = _mesh.SubMeshes.emplace_back();
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
						assert(dataStride == 2);

						const uint16_t* indData = reinterpret_cast<const uint16_t*>(data);
						indices.insert(indices.end(), indData, indData + gltfAccessor.count);
						submesh.IndexCount = gltfAccessor.count;
					}
				}
			}

			for (const auto& childID : gltfNode.children) { ParseNode(childID); }
		};
		for (const auto& nodeID : scene.nodes) { ParseNode(nodeID); }

		Log::Trace("[Graphics] - Scene contains {} materials.", model.materials.size());
		for (const auto& gltfMaterial : model.materials) {
			auto& material = _mesh.Materials.emplace_back();

			MaterialData data;
			data.AlphaCutoff = gltfMaterial.alphaCutoff;

			material.Data = _device->CreateBuffer(
				Vulkan::BufferCreateInfo(Vulkan::BufferDomain::Host, sizeof(data), vk::BufferUsageFlagBits::eUniformBuffer),
				&data);

			const auto& gltfTexture     = model.textures[gltfMaterial.pbrMetallicRoughness.baseColorTexture.index];
			const auto& gltfImage       = model.images[gltfTexture.source];
			const unsigned char* pixels = gltfImage.image.data();
			if (pixels) {
				const Vulkan::InitialImageData initialImage{.Data = pixels};
				const vk::Format format = gltfImage.component == 4 ? vk::Format::eR8G8B8A8Srgb : vk::Format::eR8G8B8Srgb;
				material.Albedo         = _device->CreateImage(
          Vulkan::ImageCreateInfo::Immutable2D(format, vk::Extent2D(gltfImage.width, gltfImage.height), true),
          &initialImage);
			} else {
				Log::Error("[Graphics] Failed to load texture: {}", stbi_failure_reason());
			}
		}
	}

	_positionBuffer = _device->CreateBuffer(
		Vulkan::BufferCreateInfo(
			Vulkan::BufferDomain::Device, sizeof(glm::vec3) * positions.size(), vk::BufferUsageFlagBits::eVertexBuffer),
		positions.data());
	_normalBuffer = _device->CreateBuffer(
		Vulkan::BufferCreateInfo(
			Vulkan::BufferDomain::Device, sizeof(glm::vec3) * normals.size(), vk::BufferUsageFlagBits::eVertexBuffer),
		normals.data());
	_texcoordBuffer = _device->CreateBuffer(
		Vulkan::BufferCreateInfo(
			Vulkan::BufferDomain::Device, sizeof(glm::vec2) * texcoords.size(), vk::BufferUsageFlagBits::eVertexBuffer),
		texcoords.data());
	_indexBuffer = _device->CreateBuffer(
		Vulkan::BufferCreateInfo(
			Vulkan::BufferDomain::Device, sizeof(uint16_t) * indices.size(), vk::BufferUsageFlagBits::eIndexBuffer),
		indices.data());
	_sceneBuffer = _device->CreateBuffer(
		Vulkan::BufferCreateInfo(Vulkan::BufferDomain::Host, sizeof(SceneData), vk::BufferUsageFlagBits::eUniformBuffer));

	auto imageData = filesystem->ReadBytes("Images/Test.jpg");
	if (imageData.has_value()) {
		int w, h;
		stbi_uc* pixels = stbi_load_from_memory(
			reinterpret_cast<const stbi_uc*>(imageData->data()), imageData->size(), &w, &h, nullptr, STBI_rgb_alpha);
		if (pixels) {
			const Vulkan::InitialImageData initialImage{.Data = pixels};
			_texture = _device->CreateImage(
				Vulkan::ImageCreateInfo::Immutable2D(vk::Format::eR8G8B8A8Unorm, vk::Extent2D(w, h), true), &initialImage);
		} else {
			Log::Error("[Graphics] Failed to load test texture: {}", stbi_failure_reason());
		}
	}

	auto vertData = filesystem->ReadBytes("Shaders/Basic.vert.spv");
	auto fragData = filesystem->ReadBytes("Shaders/Basic.frag.spv");
	if (vertData.has_value() && fragData.has_value()) {
		_program = _device->RequestProgram(vertData->size(), vertData->data(), fragData->size(), fragData->data());
	}
}

Graphics::~Graphics() noexcept {}

void Graphics::Update() {
	if (!BeginFrame()) { return; }

	const auto swapchainExtent = _swapchain->GetExtent();
	const float aspectRatio    = float(swapchainExtent.width) / float(swapchainExtent.height);
	SceneData scene{.Projection = glm::perspective(glm::radians(70.0f), aspectRatio, 0.01f, 100.0f),
	                .View       = glm::lookAt(glm::vec3(0, 1, 0), glm::vec3(1, 1, 0), glm::vec3(0, 1, 0))};
	scene.Projection[1][1] *= -1.0f;
	auto* data = _sceneBuffer->Map();
	memcpy(data, &scene, sizeof(SceneData));
	_sceneBuffer->Unmap();

	struct PushConstant {
		glm::mat4 Model;
	};
	PushConstant pc{.Model = glm::scale(glm::mat4(1.0f), glm::vec3(0.008f))};

	auto cmd = _device->RequestCommandBuffer();

	const auto clearColor = std::pow(0.1f, 2.2f);

	auto rpInfo = _device->GetStockRenderPass(Vulkan::StockRenderPass::Depth);
	rpInfo.ClearColors[0].setFloat32({clearColor, clearColor, clearColor, 1.0f});
	rpInfo.ClearDepthStencil.setDepth(1.0f);
	cmd->BeginRenderPass(rpInfo);
	cmd->SetProgram(_program);
	cmd->SetVertexAttribute(0, 0, vk::Format::eR32G32B32Sfloat, 0);
	cmd->SetVertexAttribute(1, 1, vk::Format::eR32G32B32Sfloat, 0);
	cmd->SetVertexAttribute(2, 2, vk::Format::eR32G32Sfloat, 0);
	cmd->SetVertexBinding(0, *_positionBuffer, 0, sizeof(glm::vec3), vk::VertexInputRate::eVertex);
	cmd->SetVertexBinding(1, *_normalBuffer, 0, sizeof(glm::vec3), vk::VertexInputRate::eVertex);
	cmd->SetVertexBinding(2, *_texcoordBuffer, 0, sizeof(glm::vec2), vk::VertexInputRate::eVertex);
	cmd->SetIndexBuffer(*_indexBuffer, 0, vk::IndexType::eUint16);
	cmd->SetUniformBuffer(0, 0, *_sceneBuffer);
	cmd->PushConstants(&pc, 0, sizeof(pc));
	for (const auto& submesh : _mesh.SubMeshes) {
		const auto& material = _mesh.Materials[submesh.Material];
		cmd->SetUniformBuffer(1, 0, *material.Data);
		cmd->SetTexture(1, 1, *material.Albedo->GetView(), Vulkan::StockSampler::DefaultGeometryFilterWrap);
		cmd->DrawIndexed(submesh.IndexCount, 1, submesh.FirstIndex, submesh.FirstVertex);
	}
	cmd->EndRenderPass();

	_device->Submit(cmd);

	EndFrame();
}

bool Graphics::BeginFrame() {
	_device->NextFrame();

	return _swapchain->AcquireNextImage();
}

void Graphics::EndFrame() {
	_device->EndFrame();
	_swapchain->Present();
}
}  // namespace Luna
