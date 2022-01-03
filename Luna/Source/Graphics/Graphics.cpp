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
#include <Luna/Vulkan/Sampler.hpp>
#include <Luna/Vulkan/Shader.hpp>
#include <Luna/Vulkan/Swapchain.hpp>
#include <filesystem>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace Luna {
Graphics::Graphics() {
	auto filesystem = Filesystem::Get();
	auto keyboard   = Keyboard::Get();
	auto mouse      = Mouse::Get();

	filesystem->AddSearchPath("Assets");

	const auto instanceExtensions                   = Window::Get()->GetRequiredInstanceExtensions();
	const std::vector<const char*> deviceExtensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
	_context   = std::make_unique<Vulkan::Context>(instanceExtensions, deviceExtensions);
	_device    = std::make_unique<Vulkan::Device>(*_context);
	_swapchain = std::make_unique<Vulkan::Swapchain>(*_device);

	// Create placeholder texture.
	{
		uint32_t pixels[16];
		std::fill(pixels, pixels + 16, 0xffffffff);
		const Vulkan::InitialImageData initialImage{.Data = &pixels};
		_whiteImage = _device->CreateImage(
			Vulkan::ImageCreateInfo::Immutable2D(vk::Format::eR8G8B8A8Srgb, vk::Extent2D(4, 4), false), &initialImage);
	}

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
	std::vector<uint32_t> indices;
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
				}
			}

			for (const auto& childID : gltfNode.children) { ParseNode(childID); }
		};
		for (const auto& nodeID : scene.nodes) { ParseNode(nodeID); }

		Log::Trace("[Graphics] - Scene contains {} materials.", model.materials.size());
		for (const auto& gltfMaterial : model.materials) {
			auto& material = _mesh.Materials.emplace_back();

			material.Albedo.Image   = _whiteImage;
			material.Albedo.Sampler = _device->RequestSampler(Vulkan::StockSampler::DefaultGeometryFilterWrap);
			material.Normal.Image   = _whiteImage;
			material.Normal.Sampler = _device->RequestSampler(Vulkan::StockSampler::DefaultGeometryFilterWrap);
			material.PBR.Image      = _whiteImage;
			material.PBR.Sampler    = _device->RequestSampler(Vulkan::StockSampler::DefaultGeometryFilterWrap);
			material.DualSided      = gltfMaterial.doubleSided;

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
					texture.Image = _device->CreateImage(
						Vulkan::ImageCreateInfo::Immutable2D(format, vk::Extent2D(gltfImage.width, gltfImage.height), true),
						&initialImage);
				} else {
					Log::Error("[Graphics] Failed to load texture: {}", stbi_failure_reason());
				}

				if (gltfTexture.sampler >= 0) {
					const auto& gltfSampler = model.samplers[gltfTexture.sampler];
					Vulkan::SamplerCreateInfo sampler{.MaxLod = 11.0f};
					const auto& gpuInfo = _device->GetGPUInfo();
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
					texture.Sampler = _device->RequestSampler(sampler);
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

			material.Data = _device->CreateBuffer(
				Vulkan::BufferCreateInfo(Vulkan::BufferDomain::Host, sizeof(data), vk::BufferUsageFlagBits::eUniformBuffer),
				&data);
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
			Vulkan::BufferDomain::Device, sizeof(uint32_t) * indices.size(), vk::BufferUsageFlagBits::eIndexBuffer),
		indices.data());
	_cameraBuffer = _device->CreateBuffer(
		Vulkan::BufferCreateInfo(Vulkan::BufferDomain::Host, sizeof(CameraData), vk::BufferUsageFlagBits::eUniformBuffer));
	_sceneBuffer = _device->CreateBuffer(
		Vulkan::BufferCreateInfo(Vulkan::BufferDomain::Host, sizeof(SceneData), vk::BufferUsageFlagBits::eUniformBuffer));

	LoadShaders();
	keyboard->OnKey() += [&](Key key, InputAction action, InputMods mods) -> void {
		if (key == Key::F2 && action == InputAction::Press) { LoadShaders(); }
	};
	mouse->OnButton() += [this](MouseButton button, InputAction action, InputMods mods) -> void {
		auto mouse = Mouse::Get();
		if (button == MouseButton::Button1) {
			if (action == InputAction::Press) {
				_mouseControl = true;
				mouse->SetCursorHidden(true);
			} else {
				_mouseControl = false;
				mouse->SetCursorHidden(false);
			}
		}
	};
	mouse->OnMoved() += [this](Vec2d pos) -> void {
		auto engine             = Engine::Get();
		const auto deltaTime    = engine->GetFrameDelta().Seconds();
		const float sensitivity = 5.0f * deltaTime;
		if (_mouseControl) { _camera.Rotate(pos.y * sensitivity, -pos.x * sensitivity); }
	};

	_camera.SetPosition(glm::vec3(0, 1, 0));
}

Graphics::~Graphics() noexcept {}

void Graphics::Update() {
	if (!BeginFrame()) { return; }

	// Update Camera movement.
	{
		auto engine           = Engine::Get();
		auto keyboard         = Keyboard::Get();
		const auto deltaTime  = engine->GetFrameDelta().Seconds();
		const float moveSpeed = 5.0f * deltaTime;
		if (keyboard->GetKey(Key::W) == InputAction::Press) { _camera.Move(_camera.GetForward() * moveSpeed); }
		if (keyboard->GetKey(Key::S) == InputAction::Press) { _camera.Move(-_camera.GetForward() * moveSpeed); }
		if (keyboard->GetKey(Key::A) == InputAction::Press) { _camera.Move(-_camera.GetRight() * moveSpeed); }
		if (keyboard->GetKey(Key::D) == InputAction::Press) { _camera.Move(_camera.GetRight() * moveSpeed); }
		if (keyboard->GetKey(Key::R) == InputAction::Press) { _camera.Move(_camera.GetUp() * moveSpeed); }
		if (keyboard->GetKey(Key::F) == InputAction::Press) { _camera.Move(-_camera.GetUp() * moveSpeed); }
	}

	// Update Camera buffer.
	{
		const auto swapchainExtent = _swapchain->GetExtent();
		const float aspectRatio    = float(swapchainExtent.width) / float(swapchainExtent.height);
		_camera.SetAspectRatio(aspectRatio);
		CameraData cam{.Projection = _camera.GetProjection(), .View = _camera.GetView(), .Position = _camera.GetPosition()};
		auto* data = _cameraBuffer->Map();
		memcpy(data, &cam, sizeof(CameraData));
		_cameraBuffer->Unmap();
	}

	// Update Scene buffer.
	{
		SceneData scene{.SunDirection = glm::normalize(glm::vec4(1.0f, 2.0f, 0.0f, 0.0f))};
		auto* data = _sceneBuffer->Map();
		memcpy(data, &scene, sizeof(SceneData));
		_sceneBuffer->Unmap();
	}

	struct PushConstant {
		glm::mat4 Model;
	};
	PushConstant pc{.Model = glm::scale(glm::mat4(1.0f), glm::vec3(0.008f))};

	auto cmd = _device->RequestCommandBuffer();

	auto rpInfo = _device->GetStockRenderPass(Vulkan::StockRenderPass::Depth);
	rpInfo.ClearColors[0].setFloat32({0.0f, 0.0f, 0.0f, 1.0f});
	rpInfo.ClearDepthStencil.setDepth(1.0f);
	cmd->BeginRenderPass(rpInfo);
	cmd->SetProgram(_program);
	cmd->SetVertexAttribute(0, 0, vk::Format::eR32G32B32Sfloat, 0);
	cmd->SetVertexAttribute(1, 1, vk::Format::eR32G32B32Sfloat, 0);
	cmd->SetVertexAttribute(2, 2, vk::Format::eR32G32Sfloat, 0);
	cmd->SetVertexBinding(0, *_positionBuffer, 0, sizeof(glm::vec3), vk::VertexInputRate::eVertex);
	cmd->SetVertexBinding(1, *_normalBuffer, 0, sizeof(glm::vec3), vk::VertexInputRate::eVertex);
	cmd->SetVertexBinding(2, *_texcoordBuffer, 0, sizeof(glm::vec2), vk::VertexInputRate::eVertex);
	cmd->SetIndexBuffer(*_indexBuffer, 0, vk::IndexType::eUint32);
	cmd->SetUniformBuffer(0, 0, *_cameraBuffer);
	cmd->SetUniformBuffer(0, 1, *_sceneBuffer);
	cmd->PushConstants(&pc, 0, sizeof(pc));
	for (const auto& submesh : _mesh.SubMeshes) {
		const auto& material = _mesh.Materials[submesh.Material];
		cmd->SetCullMode(material.DualSided ? vk::CullModeFlagBits::eNone : vk::CullModeFlagBits::eBack);
		cmd->SetUniformBuffer(1, 0, *material.Data);
		cmd->SetTexture(1, 1, *material.Albedo.Image->GetView(), material.Albedo.Sampler);
		cmd->SetTexture(1, 2, *material.Normal.Image->GetView(), material.Normal.Sampler);
		cmd->SetTexture(1, 3, *material.PBR.Image->GetView(), material.PBR.Sampler);
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

void Graphics::LoadShaders() {
	auto filesystem = Filesystem::Get();

	auto vert = filesystem->Read("Shaders/Basic.vert.glsl");
	auto frag = filesystem->Read("Shaders/Basic.frag.glsl");
	if (vert.has_value() && frag.has_value()) {
		auto program = _device->RequestProgram(*vert, *frag);
		if (program) { _program = program; }
	}
}
}  // namespace Luna
