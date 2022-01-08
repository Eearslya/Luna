#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wswitch"
#include <Luna/Graphics/AssetManager.hpp>
#pragma GCC diagnostic pop

#include <Luna/Core/Log.hpp>
#include <Luna/Filesystem/Filesystem.hpp>
#include <Luna/Graphics/Graphics.hpp>
#include <Luna/Scene/MeshRenderer.hpp>
#include <Luna/Scene/TransformComponent.hpp>
#include <Luna/Scene/WorldData.hpp>
#include <Luna/Threading/Threading.hpp>
#include <Luna/Vulkan/Buffer.hpp>
#include <Luna/Vulkan/CommandBuffer.hpp>
#include <Luna/Vulkan/Device.hpp>
#include <Luna/Vulkan/Fence.hpp>
#include <Luna/Vulkan/Image.hpp>
#include <Luna/Vulkan/RenderPass.hpp>
#include <Luna/Vulkan/Sampler.hpp>
#include <Tracy.hpp>
#include <filesystem>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>

namespace Luna {
constexpr static const tinygltf::FsCallbacks TinyGltfCallbacks{
	.FileExists = [](const std::string& filename, void* userData) -> bool { return Filesystem::Get()->Exists(filename); },
	.ExpandFilePath = [](const std::string& path, void* userData) -> std::string { return path; },
	.ReadWholeFile =
		[](std::vector<unsigned char>* bytes, std::string* error, const std::string& path, void* userData) -> bool {
		auto fs = Filesystem::Get();
		if (!fs->Exists(path)) {
			*error = "File not found";
			return false;
		}

		auto data = fs->ReadBytes(path);
		if (data.has_value()) {
			*bytes = data.value();
			return true;
		} else {
			*error = "Failed to open file for reading";
			return false;
		}
	},
	.WriteWholeFile = [](std::string* error,
                       const std::string& path,
                       const std::vector<unsigned char>& contents,
                       void* userData) -> bool { return false; }};

AssetManager::AssetManager() {}

AssetManager::~AssetManager() noexcept {}

void AssetManager::LoadEnvironment(const std::string& filePath, Scene& scene) {
	auto threading           = Threading::Get();
	auto loadEnvironmentTask = threading->CreateTaskGroup();
	loadEnvironmentTask->Enqueue([this, filePath, &scene]() { LoadEnvironmentTask(filePath, scene); });
	loadEnvironmentTask->Flush();
}

void AssetManager::LoadModel(const std::string& gltfFile, Scene& scene, entt::entity parentEntity) {
	auto threading = Threading::Get();

	auto loadGltfTask = threading->CreateTaskGroup();
	loadGltfTask->Enqueue([this, gltfFile, &scene, parentEntity]() { LoadGltfTask(gltfFile, scene, parentEntity); });
	loadGltfTask->Flush();
}

void AssetManager::LoadEnvironmentTask(const std::string& filePath, Scene& scene) {
	ZoneScopedN("AssetManager::LoadEnvironmentTask");

	// Determine our base filename and parent directory.
	const std::filesystem::path envPath(filePath);
	const auto envFileName  = envPath.filename().string();
	const auto envFileNameC = envFileName.c_str();
	ZoneText(envFileNameC, strlen(envFileNameC));

	ElapsedTime elapsed;

	EnvironmentHandle env = EnvironmentHandle(_environmentPool.Allocate());

	// Create our scene component.
	auto& registry        = scene.GetRegistry();
	auto rootNode         = scene.GetRoot();
	auto& worldData       = registry.get_or_emplace<WorldData>(rootNode);
	worldData.Environment = env;

	auto filesystem                           = Filesystem::Get();
	std::optional<std::vector<uint8_t>> bytes = std::nullopt;
	{
		ZoneScopedN("Filesystem Load");
		const std::string envPathStr = envPath.string();
		const char* envPathC         = envPathStr.c_str();
		ZoneText(envPathC, strlen(envPathC));
		bytes = filesystem->ReadBytes(envPath);
	}
	if (!bytes.has_value()) {
		Log::Error("[AssetManager] Failed to load environment map {}!", filePath);
		return;
	}

	int width, height, components;
	float* pixels = nullptr;
	{
		ZoneScopedN("STBI Parse");
		pixels =
			stbi_loadf_from_memory(bytes.value().data(), bytes.value().size(), &width, &height, &components, STBI_rgb_alpha);
		stbi__vertical_flip(pixels, width, height, STBI_rgb_alpha * sizeof(float));
	}
	if (!pixels) {
		Log::Error("[AssetManager] Failed to parse environment map {}!", envFileName);
		return;
	}

	auto graphics = Graphics::Get();
	auto& device  = graphics->GetDevice();

	Vulkan::Program* cubeMapProgram    = nullptr;
	Vulkan::Program* irradianceProgram = nullptr;
	{
		ZoneScopedN("Load Environment Shaders");

		auto cubeMapVertCode = filesystem->Read("Shaders/CubeMap.vert.glsl");
		if (!cubeMapVertCode.has_value()) {
			Log::Error("[AssetManager] Failed to load CubeMap vertex shader!");
			return;
		}
		auto cubeMapFragCode = filesystem->Read("Shaders/CubeMap.frag.glsl");
		if (!cubeMapFragCode.has_value()) {
			Log::Error("[AssetManager] Failed to load CubeMap fragment shader!");
			return;
		}
		auto irradianceFragCode = filesystem->Read("Shaders/CubeMapConvolute.frag.glsl");
		if (!irradianceFragCode.has_value()) {
			Log::Error("[AssetManager] Failed to load CubeMap convolution fragment shader!");
			return;
		}

		auto cubeMapVert    = device.RequestShader(vk::ShaderStageFlagBits::eVertex, cubeMapVertCode.value());
		auto cubeMapFrag    = device.RequestShader(vk::ShaderStageFlagBits::eFragment, cubeMapFragCode.value());
		auto irradianceFrag = device.RequestShader(vk::ShaderStageFlagBits::eFragment, irradianceFragCode.value());

		cubeMapProgram = device.RequestProgram(cubeMapVert, cubeMapFrag);
		if (cubeMapProgram == nullptr) {
			Log::Error("[AssetManager] Failed to load shader program for cubemap generation!");
			return;
		}
		irradianceProgram = device.RequestProgram(cubeMapVert, irradianceFrag);
		if (irradianceProgram == nullptr) {
			Log::Error("[AssetManager] Failed to load shader program for cubemap irradiance generation!");
			return;
		}
	}

	Vulkan::ImageHandle baseHdr;
	{
		ZoneScopedN("Image Creation");
		const Vulkan::InitialImageData initialData{.Data = pixels};
		const auto imageCI =
			Vulkan::ImageCreateInfo::Immutable2D(vk::Format::eR32G32B32A32Sfloat, vk::Extent2D(width, height), true);
		baseHdr = device.CreateImage(imageCI, &initialData);
	}

	Vulkan::ImageHandle skybox;
	Vulkan::ImageHandle irradiance;
	std::array<Vulkan::ImageViewHandle, 6> skyboxFaces;
	std::array<Vulkan::ImageViewHandle, 6> irradianceFaces;
	{
		ZoneScopedN("Image Creation");

		Vulkan::ImageCreateInfo cubeImageCI{
			.Domain        = Vulkan::ImageDomain::Physical,
			.Format        = vk::Format::eR16G16B16A16Sfloat,
			.Type          = vk::ImageType::e2D,
			.Usage         = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
			.Extent        = vk::Extent3D(),
			.ArrayLayers   = 6,
			.MipLevels     = 1,
			.Samples       = vk::SampleCountFlagBits::e1,
			.InitialLayout = vk::ImageLayout::eColorAttachmentOptimal,
			.Flags         = Vulkan::ImageCreateFlagBits::CreateCubeCompatible};

		const auto GetFaces = [&](Vulkan::ImageHandle& image, std::array<Vulkan::ImageViewHandle, 6>& faces) {
			for (uint32_t i = 0; i < 6; ++i) {
				const Vulkan::ImageViewCreateInfo viewCI{.Image          = image.Get(),
				                                         .Format         = cubeImageCI.Format,
				                                         .Type           = vk::ImageViewType::e2D,
				                                         .BaseMipLevel   = 0,
				                                         .MipLevels      = 1,
				                                         .BaseArrayLayer = i,
				                                         .ArrayLayers    = 1};
				faces[i] = device.CreateImageView(viewCI);
			}
		};

		cubeImageCI.Extent = vk::Extent3D(1024, 1024, 1);
		skybox             = device.CreateImage(cubeImageCI);
		GetFaces(skybox, skyboxFaces);

		cubeImageCI.Extent = vk::Extent3D(32, 32, 1);
		irradiance         = device.CreateImage(cubeImageCI);
		GetFaces(irradiance, irradianceFaces);
	}

	auto cmdBuf = device.RequestCommandBuffer(Vulkan::CommandBufferType::AsyncGraphics);

	glm::mat4 captureProjection = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);
	// captureProjection[1][1] *= -1.0f;
	const glm::mat4 captureViews[] = {
		glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f)),
		glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(-1.0f, 0.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f)),
		glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f)),
		glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(0.0f, 0.0f, -1.0f)),
		glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.0f, -1.0f, 0.0f)),
		glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, -1.0f, 0.0f))};
	struct PushConstant {
		glm::mat4 ViewProjection;
	};

	// Convert our equirectangular map into a cubemap.
	{
		ZoneScopedN("Render Cubemap");

		auto rpInfo                 = Vulkan::RenderPassInfo{};
		rpInfo.ColorAttachmentCount = 1;
		rpInfo.StoreAttachments     = 1 << 0;

		for (uint32_t i = 0; i < 6; ++i) {
			const PushConstant pc{.ViewProjection = captureProjection * captureViews[i]};
			rpInfo.ColorAttachments[0] = skyboxFaces[i].Get();
			cmdBuf->BeginRenderPass(rpInfo);
			cmdBuf->SetProgram(cubeMapProgram);
			cmdBuf->SetCullMode(vk::CullModeFlagBits::eNone);
			cmdBuf->SetTexture(0, 0, *baseHdr->GetView(), Vulkan::StockSampler::LinearClamp);
			cmdBuf->PushConstants(&pc, 0, sizeof(pc));
			cmdBuf->Draw(36);
			cmdBuf->EndRenderPass();
		}

		const vk::ImageMemoryBarrier barrier(vk::AccessFlagBits::eColorAttachmentWrite,
		                                     vk::AccessFlagBits::eShaderRead,
		                                     vk::ImageLayout::eColorAttachmentOptimal,
		                                     vk::ImageLayout::eShaderReadOnlyOptimal,
		                                     VK_QUEUE_FAMILY_IGNORED,
		                                     VK_QUEUE_FAMILY_IGNORED,
		                                     skybox->GetImage(),
		                                     vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 6));
		cmdBuf->Barrier(vk::PipelineStageFlagBits::eColorAttachmentOutput,
		                vk::PipelineStageFlagBits::eFragmentShader,
		                nullptr,
		                nullptr,
		                barrier);
	}

	// Convolute the cubemap into an irradiance map.
	{
		ZoneScopedN("Render Irradiance");

		auto rpInfo                 = Vulkan::RenderPassInfo{};
		rpInfo.ColorAttachmentCount = 1;
		rpInfo.StoreAttachments     = 1 << 0;

		for (uint32_t i = 0; i < 6; ++i) {
			const PushConstant pc{.ViewProjection = captureProjection * captureViews[i]};
			rpInfo.ColorAttachments[0] = irradianceFaces[i].Get();
			cmdBuf->BeginRenderPass(rpInfo);
			cmdBuf->SetProgram(irradianceProgram);
			cmdBuf->SetCullMode(vk::CullModeFlagBits::eNone);
			cmdBuf->SetTexture(0, 0, *skybox->GetView(), Vulkan::StockSampler::LinearClamp);
			cmdBuf->PushConstants(&pc, 0, sizeof(pc));
			cmdBuf->Draw(36);
			cmdBuf->EndRenderPass();
		}

		const vk::ImageMemoryBarrier barrier(vk::AccessFlagBits::eColorAttachmentWrite,
		                                     vk::AccessFlagBits::eShaderRead,
		                                     vk::ImageLayout::eColorAttachmentOptimal,
		                                     vk::ImageLayout::eShaderReadOnlyOptimal,
		                                     VK_QUEUE_FAMILY_IGNORED,
		                                     VK_QUEUE_FAMILY_IGNORED,
		                                     irradiance->GetImage(),
		                                     vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 6));
		cmdBuf->Barrier(vk::PipelineStageFlagBits::eColorAttachmentOutput,
		                vk::PipelineStageFlagBits::eFragmentShader,
		                nullptr,
		                nullptr,
		                barrier);
	}

	// Vulkan::FenceHandle waitFence;
	// device.Submit(cmdBuf, &waitFence);
	// if (waitFence) { waitFence->Wait(); }
	device.Submit(cmdBuf);

	env->Skybox     = std::move(skybox);
	env->Irradiance = std::move(irradiance);
	env->Ready      = true;

	elapsed.Update();
	Log::Info("[AssetManager] {} loaded in {}ms", envFileName, elapsed.Get().Milliseconds());
}

void AssetManager::LoadGltfTask(const std::string& gltfFile, Scene& scene, const entt::entity parentEntity) {
	ZoneScopedN("AssetManager::LoadGltfTask");

	// Determine our base filename and parent directory.
	const std::filesystem::path gltfPath(gltfFile);
	const auto gltfFolder    = gltfPath.parent_path().string();
	const auto gltfFileName  = gltfPath.filename().string();
	const auto gltfFileNameC = gltfFileName.c_str();
	ZoneText(gltfFileNameC, strlen(gltfFileNameC));

	// Initialize our load context.
	auto context      = _contextPool.Allocate();
	context->File     = gltfFile;
	context->FileName = gltfFileName;
	context->FilePath = gltfFolder;

	// Parse and load the glTF file.
	{
		ZoneScopedN("Parse glTF");

		// Set up the TinyglTF loader.
		tinygltf::TinyGLTF loader;
		loader.SetFsCallbacks(TinyGltfCallbacks);

		std::string gltfError;
		std::string gltfWarning;

		ElapsedTime gltfLoadTime;
		const bool loaded = loader.LoadASCIIFromFile(&context->Model, &gltfError, &gltfWarning, gltfFile);
		gltfLoadTime.Update();

		if (!gltfError.empty()) {
			Log::Error("[AssetManager] Encountered error while loading {}: {}", gltfFileName, gltfError);
		}
		if (!gltfWarning.empty()) {
			Log::Warning("[AssetManager] Encountered warning while loading {}: {}", gltfFileName, gltfWarning);
		}

		if (!loaded) {
			Log::Error("[AssetManager] Failed to load glTF model!");

			_contextPool.Free(context);

			return;
		}
	}
	const auto& gltfModel = context->Model;

	// Allocate all of the necessary asset pointers without actually loading data yet.
	{
		ZoneScopedN("Preallocation");
		for (size_t i = 0; i < gltfModel.textures.size(); ++i) { context->Textures.emplace_back(_texturePool.Allocate()); }
		for (size_t i = 0; i < gltfModel.materials.size(); ++i) {
			context->Materials.emplace_back(_materialPool.Allocate());
		}
		for (size_t i = 0; i < gltfModel.meshes.size(); ++i) { context->Meshes.emplace_back(_staticMeshPool.Allocate()); }
	}

	// Quickly traverse the node tree and create the scene entities we need.
	{
		ZoneScopedN("Scene Graph Population");
		auto rootEntity = scene.CreateEntity(context->FileName, parentEntity);

		const std::function<void(const tinygltf::Node&, entt::entity)> ParseNode = [&](const tinygltf::Node& gltfNode,
		                                                                               entt::entity parent) -> void {
			const std::string entityName = gltfNode.name.size() > 0 ? gltfNode.name : "Node";
			auto entity                  = scene.CreateEntity(entityName, parent);
			auto& registry               = scene.GetRegistry();

			auto& nodeTransform = registry.get<TransformComponent>(entity);
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
			nodeTransform.UpdateGlobalTransform(registry);

			if (gltfNode.mesh >= 0) {
				auto& meshRenderer = registry.emplace<MeshRenderer>(entity);
				meshRenderer.Mesh  = StaticMeshHandle(context->Meshes[gltfNode.mesh]);

				const auto& gltfMesh = gltfModel.meshes[gltfNode.mesh];
				for (const auto& gltfPrimitive : gltfMesh.primitives) {
					meshRenderer.Materials.push_back(MaterialHandle(context->Materials[gltfPrimitive.material]));
				}
			}

			for (const auto childId : gltfNode.children) { ParseNode(gltfModel.nodes[childId], entity); }
		};

		const int sceneId     = gltfModel.defaultScene >= 0 ? gltfModel.defaultScene : 0;
		const auto& gltfScene = gltfModel.scenes[sceneId];
		for (const auto nodeId : gltfScene.nodes) { ParseNode(gltfModel.nodes[nodeId], rootEntity); }
	}

	// Now we take all of the elements of the glTF file and load them in parallel as best we can.
	auto threading = Threading::Get();

	// Load all meshes.
	auto meshesGroup = threading->CreateTaskGroup();
	for (size_t i = 0; i < gltfModel.meshes.size(); ++i) {
		meshesGroup->Enqueue([this, context, i]() { LoadMeshTask(context, i); });
	}

	// Load all materials. These are small enough that we can use a single job for them all.
	auto materialsGroup = threading->CreateTaskGroup();
	materialsGroup->Enqueue([this, context]() { LoadMaterialsTask(context); });

	// Load all textures.
	auto texturesGroup = threading->CreateTaskGroup();
	for (size_t i = 0; i < gltfModel.textures.size(); ++i) {
		texturesGroup->Enqueue([this, context, i]() { LoadTextureTask(context, i); });
	}

	// Clean up our multithreading context.
	auto cleanupGroup = threading->CreateTaskGroup();
	cleanupGroup->Enqueue([this, context]() {
		context->LoadTime.Update();
		Log::Info("[AssetManager] {} loaded in {}ms.", context->FileName, context->LoadTime.Get().Milliseconds());
		_contextPool.Free(context);
	});
	cleanupGroup->DependOn(*meshesGroup);
	cleanupGroup->DependOn(*materialsGroup);
	cleanupGroup->DependOn(*texturesGroup);

	// Flush and submit all tasks.
	meshesGroup->Flush();
	materialsGroup->Flush();
	texturesGroup->Flush();
	cleanupGroup->Flush();
}

void AssetManager::LoadMaterialsTask(ModelLoadContext* context) const {
	ZoneScopedN("AssetManager::LoadMaterialsTask");
	ZoneText(context->FileName.c_str(), strlen(context->FileName.c_str()));

	const auto& gltfModel = context->Model;

	for (size_t materialIndex = 0; materialIndex < gltfModel.materials.size(); ++materialIndex) {
		const auto& gltfMaterial = gltfModel.materials[materialIndex];
		Material* material       = context->Materials[materialIndex];

		material->DualSided = gltfMaterial.doubleSided;
		if (gltfMaterial.pbrMetallicRoughness.baseColorFactor.size() == 4) {
			material->Data.BaseColorFactor = glm::make_vec4(gltfMaterial.pbrMetallicRoughness.baseColorFactor.data());
		}
		if (gltfMaterial.emissiveFactor.size() == 4) {
			material->Data.EmissiveFactor = glm::make_vec4(gltfMaterial.emissiveFactor.data());
		}
		material->Data.AlphaMask   = gltfMaterial.alphaMode.compare("MASK") == 0 ? 1.0f : 0.0f;
		material->Data.AlphaCutoff = gltfMaterial.alphaCutoff;
		material->Data.Metallic    = gltfMaterial.pbrMetallicRoughness.metallicFactor;
		material->Data.Roughness   = gltfMaterial.pbrMetallicRoughness.roughnessFactor;

		if (gltfMaterial.pbrMetallicRoughness.baseColorTexture.index >= 0) {
			material->Albedo = TextureHandle(context->Textures[gltfMaterial.pbrMetallicRoughness.baseColorTexture.index]);
		}
		if (gltfMaterial.normalTexture.index >= 0) {
			material->Normal = TextureHandle(context->Textures[gltfMaterial.normalTexture.index]);
		}
		if (gltfMaterial.pbrMetallicRoughness.metallicRoughnessTexture.index >= 0) {
			material->PBR =
				TextureHandle(context->Textures[gltfMaterial.pbrMetallicRoughness.metallicRoughnessTexture.index]);
		}

		material->Update();
	}
}

void AssetManager::LoadMeshTask(ModelLoadContext* context, size_t meshIndex) const {
	ZoneScopedN("AssetManager::LoadMeshTask");
	ZoneText(context->FileName.c_str(), strlen(context->FileName.c_str()));
	ZoneValue(meshIndex);

	const auto& gltfModel = context->Model;
	const auto& gltfMesh  = gltfModel.meshes[meshIndex];
	StaticMesh* mesh      = context->Meshes[meshIndex];

	struct PrimitiveContext {
		uint64_t VertexCount       = 0;
		uint64_t IndexCount        = 0;
		vk::DeviceSize FirstVertex = 0;
		vk::DeviceSize FirstIndex  = 0;
		int IndexStride            = 0;
		const void* PositionData   = nullptr;
		const void* NormalData     = nullptr;
		const void* Texcoord0Data  = nullptr;
		const void* IndexData      = nullptr;
	};

	vk::DeviceSize totalVertexCount = 0;
	vk::DeviceSize totalIndexCount  = 0;
	std::vector<PrimitiveContext> primData(gltfMesh.primitives.size());
	{
		ZoneScopedN("SubMesh Parse");
		mesh->SubMeshes.resize(gltfMesh.primitives.size());
		for (size_t prim = 0; prim < gltfMesh.primitives.size(); ++prim) {
			const auto& gltfPrimitive = gltfMesh.primitives[prim];
			if (gltfPrimitive.mode != 4) {
				Log::Warning(
					"[AssetManager] {} mesh {} contains a primitive with mode {}. Only mode 4 (triangle list) is supported.",
					context->FileName,
					meshIndex,
					gltfPrimitive.mode);
				continue;
			}

			auto& data = primData[prim];

			for (const auto [attributeName, attributeId] : gltfPrimitive.attributes) {
				const auto& gltfAccessor   = gltfModel.accessors[attributeId];
				const auto& gltfBufferView = gltfModel.bufferViews[gltfAccessor.bufferView];
				const auto& gltfBuffer     = gltfModel.buffers[gltfBufferView.buffer];
				const void* bufferData     = gltfBuffer.data.data() + gltfAccessor.byteOffset + gltfBufferView.byteOffset;

				if (attributeName.compare("POSITION") == 0) {
					data.VertexCount  = gltfAccessor.count;
					data.PositionData = bufferData;
				} else if (attributeName.compare("NORMAL") == 0) {
					data.NormalData = bufferData;
				} else if (attributeName.compare("TEXCOORD_0") == 0) {
					data.Texcoord0Data = bufferData;
				}
			}

			if (gltfPrimitive.indices >= 0) {
				const auto& gltfAccessor   = gltfModel.accessors[gltfPrimitive.indices];
				const auto& gltfBufferView = gltfModel.bufferViews[gltfAccessor.bufferView];
				const auto& gltfBuffer     = gltfModel.buffers[gltfBufferView.buffer];
				const void* bufferData     = gltfBuffer.data.data() + gltfAccessor.byteOffset + gltfBufferView.byteOffset;
				const auto bufferStride    = gltfAccessor.ByteStride(gltfBufferView);

				data.IndexCount  = gltfAccessor.count;
				data.IndexData   = bufferData;
				data.IndexStride = bufferStride;
			}

			data.FirstVertex = totalVertexCount;
			data.FirstIndex  = totalIndexCount;
			totalVertexCount += data.VertexCount;
			totalIndexCount += data.IndexCount;
		}
	}

	const vk::DeviceSize totalPositionSize  = ((totalVertexCount * sizeof(glm::vec3)) + 16llu) & ~16llu;
	const vk::DeviceSize totalNormalSize    = ((totalVertexCount * sizeof(glm::vec3)) + 16llu) & ~16llu;
	const vk::DeviceSize totalTexcoord0Size = ((totalVertexCount * sizeof(glm::vec2)) + 16llu) & ~16llu;
	const vk::DeviceSize totalIndexSize     = ((totalIndexCount * sizeof(uint32_t)) + 16llu) & ~16llu;
	const vk::DeviceSize bufferSize         = totalPositionSize + totalNormalSize + totalTexcoord0Size + totalIndexSize;

	mesh->PositionOffset  = 0;
	mesh->NormalOffset    = totalPositionSize;
	mesh->Texcoord0Offset = totalPositionSize + totalNormalSize;
	mesh->IndexOffset     = totalPositionSize + totalNormalSize + totalTexcoord0Size;

	std::unique_ptr<uint8_t[]> bufferData;
	{
		ZoneScopedN("Buffer Data Allocation");
		bufferData.reset(new uint8_t[bufferSize]);
	}
	uint8_t* positionCursor  = bufferData.get();
	uint8_t* normalCursor    = bufferData.get() + totalPositionSize;
	uint8_t* texcoord0Cursor = bufferData.get() + totalPositionSize + totalNormalSize;
	uint8_t* indexCursor     = bufferData.get() + totalPositionSize + totalNormalSize + totalTexcoord0Size;

	{
		ZoneScopedN("Buffer Data Creation");

		for (size_t prim = 0; prim < gltfMesh.primitives.size(); ++prim) {
			const auto& data = primData[prim];
			auto& submesh    = mesh->SubMeshes[prim];

			submesh.VertexCount = data.VertexCount;
			submesh.IndexCount  = data.IndexCount;
			submesh.FirstVertex = data.FirstVertex;
			submesh.FirstIndex  = data.FirstIndex;

			const size_t positionSize  = data.VertexCount * sizeof(glm::vec3);
			const size_t normalSize    = data.VertexCount * sizeof(glm::vec3);
			const size_t texcoord0Size = data.VertexCount * sizeof(glm::vec2);
			const size_t indexSize     = data.IndexCount * sizeof(uint32_t);

			memcpy(positionCursor, data.PositionData, positionSize);
			positionCursor += positionSize;

			if (data.NormalData) {
				memcpy(normalCursor, data.NormalData, normalSize);
			} else {
				memset(normalCursor, 0, normalSize);
			}
			normalCursor += normalSize;

			if (data.Texcoord0Data) {
				memcpy(texcoord0Cursor, data.Texcoord0Data, texcoord0Size);
			} else {
				memset(texcoord0Cursor, 0, texcoord0Size);
			}
			texcoord0Cursor += texcoord0Size;

			if (data.IndexData) {
				if (data.IndexStride == 1) {
					uint32_t* dst      = reinterpret_cast<uint32_t*>(indexCursor);
					const uint8_t* src = reinterpret_cast<const uint8_t*>(data.IndexData);
					for (size_t i = 0; i < data.IndexCount; ++i) { dst[i] = src[i]; }
				} else if (data.IndexStride == 2) {
					uint32_t* dst       = reinterpret_cast<uint32_t*>(indexCursor);
					const uint16_t* src = reinterpret_cast<const uint16_t*>(data.IndexData);
					for (size_t i = 0; i < data.IndexCount; ++i) { dst[i] = src[i]; }
				} else if (data.IndexStride == 4) {
					memcpy(indexCursor, data.IndexData, indexSize);
				}
			} else {
				memset(indexCursor, 0, indexSize);
			}
			indexCursor += indexSize;
		}
	}

	{
		ZoneScopedN("Buffer Creation");
		auto graphics = Graphics::Get();
		auto& device  = graphics->GetDevice();
		mesh->Buffer  = device.CreateBuffer(
      Vulkan::BufferCreateInfo(Vulkan::BufferDomain::Device,
                               bufferSize,
                               vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eIndexBuffer),
      bufferData.get());
	}

	mesh->Ready = true;
}

void AssetManager::LoadTextureTask(ModelLoadContext* context, size_t textureIndex) const {
	ZoneScopedN("AssetManager::LoadTextureTask");
	ZoneText(context->FileName.c_str(), strlen(context->FileName.c_str()));
	ZoneValue(textureIndex);

	auto filesystem         = Filesystem::Get();
	auto graphics           = Graphics::Get();
	auto& device            = graphics->GetDevice();
	const auto& gltfModel   = context->Model;
	const auto& gltfTexture = gltfModel.textures[textureIndex];
	Texture* texture        = context->Textures[textureIndex];
	if (gltfTexture.source < 0) {
		Log::Error("[AssetManager] {} texture {} does not specify a source image!", context->FileName, textureIndex);
		return;
	}

	const auto& gltfImage                     = gltfModel.images[gltfTexture.source];
	const auto uri                            = gltfImage.uri;
	const std::filesystem::path imagePath     = std::filesystem::path(context->FilePath) / uri;
	std::optional<std::vector<uint8_t>> bytes = std::nullopt;
	{
		ZoneScopedN("Filesystem Load");
		const std::string imagePathStr = imagePath.string();
		const char* imagePathC         = imagePathStr.c_str();
		ZoneText(imagePathC, strlen(imagePathC));
		bytes = filesystem->ReadBytes(imagePath);
	}
	if (!bytes.has_value()) {
		Log::Error("[AssetManager] Failed to load texture for {}: {}", context->FileName, uri);
		return;
	}

	int width, height, components;
	stbi_uc* pixels = nullptr;
	{
		ZoneScopedN("STBI Parse");
		pixels =
			stbi_load_from_memory(bytes.value().data(), bytes.value().size(), &width, &height, &components, STBI_rgb_alpha);
	}
	if (pixels == nullptr) {
		Log::Error(
			"[AssetManager] Failed to read texture data for {}, {}: {}", context->FileName, uri, stbi_failure_reason());
		return;
	}

	{
		ZoneScopedN("Image Creation");
		const Vulkan::InitialImageData initialData{.Data = pixels};
		const auto imageCI =
			Vulkan::ImageCreateInfo::Immutable2D(vk::Format::eR8G8B8A8Unorm, vk::Extent2D(width, height), true);
		texture->Image = device.CreateImage(imageCI, &initialData);
	}

	{
		ZoneScopedN("Sampler Creation");
		const auto gpuInfo     = device.GetGPUInfo();
		const float anisotropy = gpuInfo.EnabledFeatures.Features.samplerAnisotropy
		                           ? gpuInfo.Properties.Properties.limits.maxSamplerAnisotropy
		                           : 0.0f;
		Vulkan::SamplerCreateInfo samplerCI{
			.AnisotropyEnable = anisotropy > 0.0f, .MaxAnisotropy = anisotropy, .MaxLod = 11.0f};
		if (gltfTexture.sampler >= 0) {
			const auto& gltfSampler = gltfModel.samplers[gltfTexture.sampler];
			switch (gltfSampler.magFilter) {
				case 9728:  // NEAREST
					samplerCI.MagFilter = vk::Filter::eNearest;
					break;
				case 9729:  // LINEAR
					samplerCI.MagFilter = vk::Filter::eLinear;
					break;
			}
			switch (gltfSampler.minFilter) {
				case 9728:  // NEAREST
					samplerCI.MinFilter = vk::Filter::eNearest;
					break;
				case 9729:  // LINEAR
					samplerCI.MinFilter = vk::Filter::eLinear;
					break;
				case 9984:  // NEAREST_MIPMAP_NEAREST
					samplerCI.MinFilter  = vk::Filter::eNearest;
					samplerCI.MipmapMode = vk::SamplerMipmapMode::eNearest;
					break;
				case 9985:  // LINEAR_MIPMAP_NEAREST
					samplerCI.MinFilter  = vk::Filter::eLinear;
					samplerCI.MipmapMode = vk::SamplerMipmapMode::eNearest;
					break;
				case 9986:  // NEAREST_MIPMAP_LINEAR
					samplerCI.MinFilter  = vk::Filter::eNearest;
					samplerCI.MipmapMode = vk::SamplerMipmapMode::eLinear;
					break;
				case 9987:  // LINEAR_MIPMAP_LINEAR
					samplerCI.MinFilter  = vk::Filter::eLinear;
					samplerCI.MipmapMode = vk::SamplerMipmapMode::eLinear;
					break;
			}
			switch (gltfSampler.wrapS) {
				case 33071:  // CLAMP_TO_EDGE
					samplerCI.AddressModeU = vk::SamplerAddressMode::eClampToEdge;
					break;
				case 33648:  // MIRRORED_REPEAT
					samplerCI.AddressModeU = vk::SamplerAddressMode::eMirroredRepeat;
					break;
				case 10497:  // REPEAT
					samplerCI.AddressModeU = vk::SamplerAddressMode::eRepeat;
					break;
			}
			switch (gltfSampler.wrapT) {
				case 33071:  // CLAMP_TO_EDGE
					samplerCI.AddressModeV = vk::SamplerAddressMode::eClampToEdge;
					break;
				case 33648:  // MIRRORED_REPEAT
					samplerCI.AddressModeV = vk::SamplerAddressMode::eMirroredRepeat;
					break;
				case 10497:  // REPEAT
					samplerCI.AddressModeV = vk::SamplerAddressMode::eRepeat;
					break;
			}
		}
		texture->Sampler = device.RequestSampler(samplerCI);
	}

	texture->Ready.store(true);
}

void AssetManager::FreeEnvironment(Environment* environment) {
	_environmentPool.Free(environment);
}

void AssetManager::FreeMaterial(Material* material) {
	_materialPool.Free(material);
}

void AssetManager::FreeStaticMesh(StaticMesh* mesh) {
	_staticMeshPool.Free(mesh);
}

void AssetManager::FreeTexture(Texture* texture) {
	_texturePool.Free(texture);
}
}  // namespace Luna
