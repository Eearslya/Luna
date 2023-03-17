#include <imgui.h>
#include <stb_image.h>

#include <Luna/Application/Input.hpp>
#include <Luna/Luna.hpp>
#include <Luna/Renderer/RenderGraph.hpp>
#include <Luna/Renderer/RenderPass.hpp>
#include <Luna/Utility/Log.hpp>
#include <Luna/Vulkan/Buffer.hpp>
#include <Luna/Vulkan/CommandBuffer.hpp>
#include <Luna/Vulkan/Device.hpp>
#include <Luna/Vulkan/ImGuiRenderer.hpp>
#include <Luna/Vulkan/Image.hpp>
#include <Luna/Vulkan/RenderPass.hpp>
#include <Luna/Vulkan/ShaderCompiler.hpp>
#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <utility>

#include "Environment.hpp"
#include "Files.hpp"
#include "IconsFontAwesome6.h"
#include "Model.hpp"

static size_t SizeAligned(size_t size, size_t align) {
	return (size + align - 1) & ~(align - 1);
}

struct UniformBuffer {
	glm::mat4 Projection;
	glm::mat4 View;
	glm::mat4 Model;
};

struct ObjectData {
	vk::DeviceAddress VertexAddress;
	vk::DeviceAddress IndexAddress;
};

struct UniformBufferRT {
	glm::mat4 ViewInverse;
	glm::mat4 ProjectionInverse;
};

struct SceneUBO {
	glm::mat4 Projection;
	glm::mat4 View;
	glm::mat4 ViewProjection;
	glm::vec4 ViewPosition;
	glm::vec4 SunPosition;
	float Exposure;
	float Gamma;
	float PrefilteredMipLevels;
	float IBLStrength;
};

struct PushConstant {
	glm::mat4 Node = glm::mat4(1.0f);
};

template <typename T>
class UniformBufferSet {
 public:
	UniformBufferSet(Luna::Vulkan::Device& device) : _device(device) {
		const uint32_t frames = device.GetFramesInFlight();
		const Luna::Vulkan::BufferCreateInfo bufferCI{
			Luna::Vulkan::BufferDomain::Host, sizeof(T), vk::BufferUsageFlagBits::eUniformBuffer};
		for (uint32_t i = 0; i < frames; ++i) { _buffers.push_back(_device.CreateBuffer(bufferCI)); }
	}

	T& Data() {
		return _data;
	}
	const T& Data() const {
		return _data;
	}

	void Bind(Luna::Vulkan::CommandBuffer& cmd, uint32_t set, uint32_t binding) {
		Flush();
		cmd.SetUniformBuffer(set, binding, *_buffers[_device.GetFrameIndex()], 0, sizeof(T));
	}

	void Flush() {
		const auto& buffer = _buffers[_device.GetFrameIndex()];
		void* bufferData   = buffer->Map();
		memcpy(bufferData, &_data, sizeof(T));
	}

 private:
	Luna::Vulkan::Device& _device;
	std::vector<Luna::Vulkan::BufferHandle> _buffers;
	T _data;
};

class ViewerApplication : public Luna::Application {
 public:
	virtual void OnStart() override {
		auto& device = GetDevice();

		_renderGraph = std::make_unique<Luna::RenderGraph>(device);

		_swapchainConfig = GetSwapchainConfig();
		OnSwapchainChanged += [&](const Luna::Vulkan::SwapchainConfiguration& config) {
			_swapchainConfig = config;
			_swapchainDirty  = true;
		};

		StyleImGui();

		// Default Images
		{
			uint32_t pixel;
			std::array<Luna::Vulkan::ImageInitialData, 6> initialImages;
			for (int i = 0; i < 6; ++i) { initialImages[i] = Luna::Vulkan::ImageInitialData{.Data = &pixel}; }
			const Luna::Vulkan::ImageCreateInfo imageCI2D = {
				.Domain        = Luna::Vulkan::ImageDomain::Physical,
				.Width         = 1,
				.Height        = 1,
				.Depth         = 1,
				.MipLevels     = 1,
				.ArrayLayers   = 1,
				.Format        = vk::Format::eR8G8B8A8Unorm,
				.InitialLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
				.Type          = vk::ImageType::e2D,
				.Usage         = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eInputAttachment,
				.Samples       = vk::SampleCountFlagBits::e1,
			};
			const Luna::Vulkan::ImageCreateInfo imageCICube = {
				.Domain        = Luna::Vulkan::ImageDomain::Physical,
				.Width         = 1,
				.Height        = 1,
				.Depth         = 1,
				.MipLevels     = 1,
				.ArrayLayers   = 6,
				.Format        = vk::Format::eR8G8B8A8Unorm,
				.InitialLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
				.Type          = vk::ImageType::e2D,
				.Usage         = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eInputAttachment,
				.Samples       = vk::SampleCountFlagBits::e1,
				.Flags         = vk::ImageCreateFlagBits::eCubeCompatible,
			};

			pixel                    = 0xff000000;
			_defaultImages.Black2D   = device.CreateImage(imageCI2D, initialImages.data());
			_defaultImages.BlackCube = device.CreateImage(imageCICube, initialImages.data());

			pixel                 = 0xff888888;
			_defaultImages.Gray2D = device.CreateImage(imageCI2D, initialImages.data());

			pixel                   = 0xffff8888;
			_defaultImages.Normal2D = device.CreateImage(imageCI2D, initialImages.data());

			pixel                    = 0xffffffff;
			_defaultImages.White2D   = device.CreateImage(imageCI2D, initialImages.data());
			_defaultImages.WhiteCube = device.CreateImage(imageCICube, initialImages.data());
		}

		_sceneUBO = std::make_unique<UniformBufferSet<SceneUBO>>(device);

		_environment = std::make_unique<Environment>(device, "Assets/Environments/TokyoBigSight.hdr");
		_model       = std::make_unique<Model>(device, "Assets/Models/DamagedHelmet/DamagedHelmet.gltf");

		Luna::Input::OnKey += [&](Luna::Key key, Luna::InputAction action, Luna::InputMods mods) {
			if (action == Luna::InputAction::Press && key == Luna::Key::F5) { LoadShaders(); }
		};
		LoadShaders();

#if 1
		// Bottom-Level Acceleration Structure
		{
			const auto& mesh              = _model->Meshes[0];
			const auto& meshBuffer        = mesh->Buffer;
			const auto meshBufferAddress  = meshBuffer->GetDeviceAddress();
			const auto vertexAddress      = meshBufferAddress;
			const auto indexAddress       = meshBufferAddress + mesh->IndexOffset;
			const uint32_t primitiveCount = mesh->TotalIndexCount / 3;

			const vk::AccelerationStructureGeometryTrianglesDataKHR geometryTriangles(vk::Format::eR32G32B32Sfloat,
			                                                                          vertexAddress,
			                                                                          sizeof(Vertex),
			                                                                          mesh->TotalVertexCount,
			                                                                          vk::IndexType::eUint32,
			                                                                          indexAddress,
			                                                                          nullptr);
			const vk::AccelerationStructureGeometryKHR geometry(
				vk::GeometryTypeKHR::eTriangles, geometryTriangles, vk::GeometryFlagBitsKHR::eOpaque);
			vk::AccelerationStructureBuildGeometryInfoKHR buildGeometry(
				vk::AccelerationStructureTypeKHR::eBottomLevel,
				vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace,
				vk::BuildAccelerationStructureModeKHR::eBuild,
				VK_NULL_HANDLE,
				VK_NULL_HANDLE,
				geometry,
				nullptr,
				nullptr);

			const vk::AccelerationStructureBuildSizesInfoKHR buildSizes =
				device.GetDevice().getAccelerationStructureBuildSizesKHR(
					vk::AccelerationStructureBuildTypeKHR::eDevice, buildGeometry, primitiveCount);

			const Luna::Vulkan::BufferCreateInfo blasBufferCI{
				Luna::Vulkan::BufferDomain::Device,
				buildSizes.accelerationStructureSize,
				vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress};
			_blasBuffer = device.CreateBuffer(blasBufferCI);

			const Luna::Vulkan::BufferCreateInfo blasScratchCI{
				Luna::Vulkan::BufferDomain::Device,
				buildSizes.buildScratchSize,
				vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress};
			auto scratchBuffer = device.CreateBuffer(blasScratchCI);

			const vk::AccelerationStructureCreateInfoKHR blasCI({},
			                                                    _blasBuffer->GetBuffer(),
			                                                    0,
			                                                    _blasBuffer->GetCreateInfo().Size,
			                                                    vk::AccelerationStructureTypeKHR::eBottomLevel,
			                                                    0);
			_blas = device.GetDevice().createAccelerationStructureKHR(blasCI);
			Luna::Log::Debug("Vulkan", "Acceleration Structure created.");

			buildGeometry.dstAccelerationStructure = _blas;
			buildGeometry.scratchData              = scratchBuffer->GetDeviceAddress();

			const vk::AccelerationStructureBuildRangeInfoKHR buildRange(primitiveCount, 0, 0, 0);
			const std::vector<const vk::AccelerationStructureBuildRangeInfoKHR*> buildRanges = {&buildRange};

			auto cmdHandle = device.RequestCommandBuffer();
			auto cmd       = cmdHandle->GetCommandBuffer();
			cmd.buildAccelerationStructuresKHR(buildGeometry, buildRanges);
			device.Submit(cmdHandle);
			device.WaitIdle();

			const vk::AccelerationStructureDeviceAddressInfoKHR blasAddressInfo(_blas);
			_blasAddress = device.GetDevice().getAccelerationStructureAddressKHR(blasAddressInfo);
		}

		// Top-Level Acceleration Structure
		{
			auto nodeTransform = glm::inverse(_model->RootNodes[0]->GetGlobalTransform());
			vk::TransformMatrixKHR transform;
			transform.matrix[0][0] = nodeTransform[0][0];
			transform.matrix[0][1] = nodeTransform[0][1];
			transform.matrix[0][2] = nodeTransform[0][2];
			transform.matrix[0][3] = nodeTransform[0][3];
			transform.matrix[1][0] = nodeTransform[1][0];
			transform.matrix[1][1] = nodeTransform[1][1];
			transform.matrix[1][2] = nodeTransform[1][2];
			transform.matrix[1][3] = nodeTransform[1][3];
			transform.matrix[2][0] = nodeTransform[2][0];
			transform.matrix[2][1] = nodeTransform[2][1];
			transform.matrix[2][2] = nodeTransform[2][2];
			transform.matrix[2][3] = nodeTransform[2][3];

			const vk::AccelerationStructureInstanceKHR instance(
				transform, 0, 0xff, 0, vk::GeometryInstanceFlagBitsKHR::eTriangleFacingCullDisable, _blasAddress);

			transform.matrix[0][3] = -2.0f;
			const vk::AccelerationStructureInstanceKHR instance2(
				transform, 0, 0xff, 0, vk::GeometryInstanceFlagBitsKHR::eTriangleFacingCullDisable, _blasAddress);

			std::vector<vk::AccelerationStructureInstanceKHR> instanceArray = {instance, instance2};

			const uint32_t primitiveCount = instanceArray.size();

			const Luna::Vulkan::BufferCreateInfo instanceBufferCI{
				Luna::Vulkan::BufferDomain::Host,
				sizeof(vk::AccelerationStructureInstanceKHR) * primitiveCount,
				vk::BufferUsageFlagBits::eShaderDeviceAddress |
					vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR};
			auto instanceBuffer              = device.CreateBuffer(instanceBufferCI, instanceArray.data());
			const auto instanceBufferAddress = instanceBuffer->GetDeviceAddress();

			const vk::AccelerationStructureGeometryInstancesDataKHR instances(VK_FALSE, instanceBufferAddress);
			const vk::AccelerationStructureGeometryKHR geometry(
				vk::GeometryTypeKHR::eInstances, instances, vk::GeometryFlagBitsKHR::eOpaque);

			vk::AccelerationStructureBuildGeometryInfoKHR buildGeometry(
				vk::AccelerationStructureTypeKHR::eTopLevel,
				vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace,
				vk::BuildAccelerationStructureModeKHR::eBuild,
				VK_NULL_HANDLE,
				VK_NULL_HANDLE,
				geometry,
				nullptr,
				nullptr);
			const vk::AccelerationStructureBuildSizesInfoKHR buildSizes =
				device.GetDevice().getAccelerationStructureBuildSizesKHR(
					vk::AccelerationStructureBuildTypeKHR::eDevice, buildGeometry, primitiveCount);

			const Luna::Vulkan::BufferCreateInfo tlasBufferCI{
				Luna::Vulkan::BufferDomain::Device,
				buildSizes.accelerationStructureSize,
				vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress};
			_tlasBuffer = device.CreateBuffer(tlasBufferCI);

			const Luna::Vulkan::BufferCreateInfo tlasScratchCI{
				Luna::Vulkan::BufferDomain::Device,
				buildSizes.buildScratchSize,
				vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress};
			auto scratchBuffer = device.CreateBuffer(tlasScratchCI);

			const vk::AccelerationStructureCreateInfoKHR tlasCI({},
			                                                    _tlasBuffer->GetBuffer(),
			                                                    0,
			                                                    _tlasBuffer->GetCreateInfo().Size,
			                                                    vk::AccelerationStructureTypeKHR::eTopLevel,
			                                                    0);
			_tlas = device.GetDevice().createAccelerationStructureKHR(tlasCI);
			Luna::Log::Debug("Vulkan", "Acceleration Structure created.");

			buildGeometry.dstAccelerationStructure = _tlas;
			buildGeometry.scratchData              = scratchBuffer->GetDeviceAddress();

			const vk::AccelerationStructureBuildRangeInfoKHR buildRange(primitiveCount, 0, 0, 0);
			const std::vector<const vk::AccelerationStructureBuildRangeInfoKHR*> buildRanges = {&buildRange};

			auto cmdHandle = device.RequestCommandBuffer();
			auto cmd       = cmdHandle->GetCommandBuffer();
			cmd.buildAccelerationStructuresKHR(buildGeometry, buildRanges);
			device.Submit(cmdHandle);
			device.WaitIdle();

			const vk::AccelerationStructureDeviceAddressInfoKHR tlasAddressInfo(_blas);
			_tlasAddress = device.GetDevice().getAccelerationStructureAddressKHR(tlasAddressInfo);
		}

		const Luna::Vulkan::BufferCreateInfo rtuboCI{
			Luna::Vulkan::BufferDomain::Host, sizeof(UniformBufferRT), vk::BufferUsageFlagBits::eUniformBuffer};
		_rtUniform = device.CreateBuffer(rtuboCI);

		{
			const vk::DescriptorSetLayoutBinding tlasBind(
				0, vk::DescriptorType::eAccelerationStructureKHR, 1, vk::ShaderStageFlagBits::eRaygenKHR);
			const vk::DescriptorSetLayoutBinding imageBind(
				1, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eRaygenKHR);
			const vk::DescriptorSetLayoutBinding uniformBind(
				2, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eRaygenKHR);
			const std::vector<vk::DescriptorSetLayoutBinding> binds = {tlasBind, imageBind, uniformBind};
			const vk::PushConstantRange pushConstant(vk::ShaderStageFlagBits::eClosestHitKHR, 0, sizeof(ObjectData));
			const vk::DescriptorSetLayoutCreateInfo layoutCI({}, binds);
			_rtSetLayout = device.GetDevice().createDescriptorSetLayout(layoutCI);
			const vk::PipelineLayoutCreateInfo pipelineLayoutCI({}, _rtSetLayout, pushConstant);
			_rtPipelineLayout = device.GetDevice().createPipelineLayout(pipelineLayoutCI);
			Luna::Log::Debug("Vulkan", "Pipeline Layout created.");

			const std::vector<vk::DescriptorPoolSize> poolSizes = {{vk::DescriptorType::eAccelerationStructureKHR, 1},
			                                                       {vk::DescriptorType::eStorageImage, 1},
			                                                       { vk::DescriptorType::eUniformBuffer,
				                                                       1 }};
			const vk::DescriptorPoolCreateInfo poolCI({}, 1, poolSizes);
			_rtPool = device.GetDevice().createDescriptorPool(poolCI);
			Luna::Log::Debug("Vulkan", "Descriptor Pool created.");
			const vk::DescriptorSetAllocateInfo setAI(_rtPool, _rtSetLayout);
			auto sets = device.GetDevice().allocateDescriptorSets(setAI);
			_rtSet    = sets[0];
			Luna::Log::Debug("Vulkan", "Descriptor Set allocated.");

			auto& compiler = device.GetShaderCompiler();
			auto rgenGlsl  = ReadFile("Resources/Shaders/RT.rgen.glsl");
			auto missGlsl  = ReadFile("Resources/Shaders/RT.rmiss.glsl");
			auto chitGlsl  = ReadFile("Resources/Shaders/RT.chit.glsl");
			auto rgenSpv   = compiler.Compile(vk::ShaderStageFlagBits::eRaygenKHR, rgenGlsl);
			auto missSpv   = compiler.Compile(vk::ShaderStageFlagBits::eMissKHR, missGlsl);
			auto chitSpv   = compiler.Compile(vk::ShaderStageFlagBits::eClosestHitKHR, chitGlsl);
			const vk::ShaderModuleCreateInfo rgenCI({}, (*rgenSpv).size() * sizeof(uint32_t), (*rgenSpv).data());
			const vk::ShaderModuleCreateInfo missCI({}, (*missSpv).size() * sizeof(uint32_t), (*missSpv).data());
			const vk::ShaderModuleCreateInfo chitCI({}, (*chitSpv).size() * sizeof(uint32_t), (*chitSpv).data());
			auto rgen = device.GetDevice().createShaderModule(rgenCI);
			auto miss = device.GetDevice().createShaderModule(missCI);
			auto chit = device.GetDevice().createShaderModule(chitCI);

			std::array<vk::PipelineShaderStageCreateInfo, 3> shaderStages;
			std::array<vk::RayTracingShaderGroupCreateInfoKHR, 3> shaderGroups;

			shaderStages[0] = vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eRaygenKHR, rgen, "main");
			shaderStages[1] = vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eMissKHR, miss, "main");
			shaderStages[2] = vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eClosestHitKHR, chit, "main");
			shaderGroups[0] = vk::RayTracingShaderGroupCreateInfoKHR(vk::RayTracingShaderGroupTypeKHR::eGeneral,
			                                                         0,
			                                                         VK_SHADER_UNUSED_KHR,
			                                                         VK_SHADER_UNUSED_KHR,
			                                                         VK_SHADER_UNUSED_KHR,
			                                                         nullptr);
			shaderGroups[1] = vk::RayTracingShaderGroupCreateInfoKHR(vk::RayTracingShaderGroupTypeKHR::eGeneral,
			                                                         1,
			                                                         VK_SHADER_UNUSED_KHR,
			                                                         VK_SHADER_UNUSED_KHR,
			                                                         VK_SHADER_UNUSED_KHR,
			                                                         nullptr);
			shaderGroups[2] = vk::RayTracingShaderGroupCreateInfoKHR(vk::RayTracingShaderGroupTypeKHR::eTrianglesHitGroup,
			                                                         VK_SHADER_UNUSED_KHR,
			                                                         2,
			                                                         VK_SHADER_UNUSED_KHR,
			                                                         VK_SHADER_UNUSED_KHR,
			                                                         nullptr);

			const vk::RayTracingPipelineCreateInfoKHR pipelineCI(
				{}, shaderStages, shaderGroups, 1, nullptr, nullptr, nullptr, _rtPipelineLayout, VK_NULL_HANDLE, 0);
			auto pipeline = device.GetDevice().createRayTracingPipelineKHR(VK_NULL_HANDLE, VK_NULL_HANDLE, pipelineCI);
			_rtPipeline   = pipeline.value;
			Luna::Log::Debug("Vulkan", "Ray Tracing Pipeline created.");

			const uint32_t handleSize = device.GetDeviceInfo().Properties.RayTracingPipeline.shaderGroupHandleSize;
			const uint32_t handleSizeAligned =
				SizeAligned(handleSize, device.GetDeviceInfo().Properties.RayTracingPipeline.shaderGroupHandleAlignment);
			const uint32_t groupCount = shaderGroups.size();
			const size_t sbtSize      = groupCount * handleSizeAligned;

			std::vector<uint8_t> shaderHandleStorage =
				device.GetDevice().getRayTracingShaderGroupHandlesKHR<uint8_t>(_rtPipeline, 0u, groupCount, sbtSize);

			const Luna::Vulkan::BufferCreateInfo bufferCI{
				Luna::Vulkan::BufferDomain::Host,
				handleSize,
				vk::BufferUsageFlagBits::eShaderBindingTableKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress};
			_rgenTable = device.CreateBuffer(bufferCI, shaderHandleStorage.data());
			_missTable = device.CreateBuffer(bufferCI, shaderHandleStorage.data() + handleSizeAligned);
			_chitTable = device.CreateBuffer(bufferCI, shaderHandleStorage.data() + (handleSizeAligned * 2));
		}
#endif
	}

	virtual void OnUpdate() override {
		auto& device = GetDevice();

		if (!_rtImage) {
			const Luna::Vulkan::ImageCreateInfo rtImageCI{
				.Width  = _swapchainConfig.Extent.width,
				.Height = _swapchainConfig.Extent.height,
				.Format = vk::Format::eR8G8B8A8Unorm,
				.Usage  = vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eStorage};
			_rtImage = device.CreateImage(rtImageCI);
		}

		UniformBufferRT ubo;
		_sceneSize              = GetFramebufferSize();
		const float aspectRatio = float(_sceneSize.x) / float(_sceneSize.y);
		glm::mat4 persp         = glm::perspective(glm::radians(60.0f), aspectRatio, 0.01f, 1000.0f);
		persp[1][1] *= -1.0;
		ubo.ProjectionInverse = glm::inverse(persp);
		ubo.ViewInverse       = glm::inverse(glm::lookAt(glm::vec3(1, 0.5f, 2), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0)));
		memcpy(_rtUniform->Map(), &ubo, sizeof(ubo));

		const vk::WriteDescriptorSetAccelerationStructureKHR asWrite(_tlas);
		const vk::DescriptorImageInfo imageInfo(VK_NULL_HANDLE, _rtImage->GetView().GetView(), vk::ImageLayout::eGeneral);
		const vk::DescriptorBufferInfo bufferInfo(_rtUniform->GetBuffer(), 0, VK_WHOLE_SIZE);

		const vk::WriteDescriptorSet tlasWrite(
			_rtSet, 0, 0, 1, vk::DescriptorType::eAccelerationStructureKHR, nullptr, nullptr, nullptr, &asWrite);
		const vk::WriteDescriptorSet imageWrite(_rtSet, 1, 0, 1, vk::DescriptorType::eStorageImage, &imageInfo);
		const vk::WriteDescriptorSet bufferWrite(_rtSet, 2, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &bufferInfo);
		device.GetDevice().updateDescriptorSets({tlasWrite, imageWrite, bufferWrite}, nullptr);

		const uint32_t handleSizeAligned =
			SizeAligned(device.GetDeviceInfo().Properties.RayTracingPipeline.shaderGroupHandleSize,
		              device.GetDeviceInfo().Properties.RayTracingPipeline.shaderGroupHandleAlignment);

		const vk::StridedDeviceAddressRegionKHR rgenSbt(
			_rgenTable->GetDeviceAddress(), handleSizeAligned, handleSizeAligned);
		const vk::StridedDeviceAddressRegionKHR missSbt(
			_missTable->GetDeviceAddress(), handleSizeAligned, handleSizeAligned);
		const vk::StridedDeviceAddressRegionKHR chitSbt(
			_chitTable->GetDeviceAddress(), handleSizeAligned, handleSizeAligned);
		const vk::StridedDeviceAddressRegionKHR callSbt;

		ObjectData pc    = {};
		pc.VertexAddress = _model->Meshes[0]->Buffer->GetDeviceAddress();
		pc.IndexAddress  = _model->Meshes[0]->Buffer->GetDeviceAddress() + _model->Meshes[0]->IndexOffset;

		auto cmdH = device.RequestCommandBuffer();
		auto cmd  = cmdH->GetCommandBuffer();

		cmd.bindPipeline(vk::PipelineBindPoint::eRayTracingKHR, _rtPipeline);
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eRayTracingKHR, _rtPipelineLayout, 0, _rtSet, nullptr);
		cmd.pushConstants<ObjectData>(_rtPipelineLayout, vk::ShaderStageFlagBits::eClosestHitKHR, 0, pc);
		cmd.traceRaysKHR(

			rgenSbt, missSbt, chitSbt, callSbt, _swapchainConfig.Extent.width, _swapchainConfig.Extent.height, 1);

		const vk::ImageMemoryBarrier swapDst({},
		                                     vk::AccessFlagBits::eTransferWrite,
		                                     vk::ImageLayout::eUndefined,
		                                     vk::ImageLayout::eTransferDstOptimal,
		                                     VK_QUEUE_FAMILY_IGNORED,
		                                     VK_QUEUE_FAMILY_IGNORED,
		                                     device.GetSwapchainView().GetImage()->GetImage(),
		                                     vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1));
		const vk::ImageMemoryBarrier rtSrc(vk::AccessFlagBits::eShaderWrite,
		                                   vk::AccessFlagBits::eTransferRead,
		                                   vk::ImageLayout::eGeneral,
		                                   vk::ImageLayout::eTransferSrcOptimal,
		                                   VK_QUEUE_FAMILY_IGNORED,
		                                   VK_QUEUE_FAMILY_IGNORED,
		                                   _rtImage->GetImage(),
		                                   vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1));
		cmd.pipelineBarrier(vk::PipelineStageFlagBits::eRayTracingShaderKHR,
		                    vk::PipelineStageFlagBits::eTransfer,
		                    {},
		                    nullptr,
		                    nullptr,
		                    {swapDst, rtSrc});
		cmdH->BlitImage(*device.GetSwapchainView().GetImage(),
		                *_rtImage,
		                {},
		                vk::Offset3D(_swapchainConfig.Extent.width, _swapchainConfig.Extent.height, 1),
		                {},
		                vk::Offset3D(_swapchainConfig.Extent.width, _swapchainConfig.Extent.height, 1),
		                0,
		                0);
		const vk::ImageMemoryBarrier swapPresent(vk::AccessFlagBits::eTransferWrite,
		                                         {},
		                                         vk::ImageLayout::eTransferDstOptimal,
		                                         vk::ImageLayout::ePresentSrcKHR,
		                                         VK_QUEUE_FAMILY_IGNORED,
		                                         VK_QUEUE_FAMILY_IGNORED,
		                                         device.GetSwapchainView().GetImage()->GetImage(),
		                                         vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1));
		const vk::ImageMemoryBarrier rtGeneral(vk::AccessFlagBits::eTransferRead,
		                                       {},
		                                       vk::ImageLayout::eTransferSrcOptimal,
		                                       vk::ImageLayout::eGeneral,
		                                       VK_QUEUE_FAMILY_IGNORED,
		                                       VK_QUEUE_FAMILY_IGNORED,
		                                       _rtImage->GetImage(),
		                                       vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1));
		cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
		                    vk::PipelineStageFlagBits::eBottomOfPipe,
		                    {},
		                    nullptr,
		                    nullptr,
		                    {swapPresent, rtGeneral});

		cmdH->TouchSwapchain(vk::PipelineStageFlagBits2::eTransfer);
		device.Submit(cmdH);
		device.WaitIdle();
#if 0
		if (_swapchainDirty) {
			BakeRenderGraph();
			_swapchainDirty = false;
		}

		// Update Uniform Buffer
		auto& sceneData         = _sceneUBO->Data();
		_sceneSize              = GetFramebufferSize();
		const float aspectRatio = float(_sceneSize.x) / float(_sceneSize.y);
#	if 0
		const float fovY     = aspectRatio >= (16.0f / 9.0f) ? 90.0f / aspectRatio : 90.0f;
		sceneData.Projection = glm::perspective(glm::radians(fovY), aspectRatio, 0.01f, 1000.0f);
#	else
		sceneData.Projection                      = glm::perspective(glm::radians(60.0f), aspectRatio, 0.01f, 1000.0f);
#	endif
		sceneData.View                 = glm::lookAt(glm::vec3(1, 0.5f, 2), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
		sceneData.ViewProjection       = sceneData.Projection * sceneData.View;
		sceneData.ViewPosition         = glm::vec4(1, 0.5f, 2, 1.0f);
		sceneData.SunPosition          = glm::vec4(10.0f, 10.0f, 10.0f, 1.0f);
		sceneData.Exposure             = 4.5f;
		sceneData.Gamma                = 2.2f;
		sceneData.PrefilteredMipLevels = _environment ? _environment->Prefiltered->GetCreateInfo().MipLevels : 1;
		sceneData.IBLStrength          = _environment ? 1.0f : 0.0f;

		Luna::TaskComposer composer;
		_renderGraph->SetupAttachments(&device.GetSwapchainView());
		_renderGraph->EnqueueRenderPasses(device, composer);
		auto final = composer.GetOutgoingTask();
		final->Wait();
#endif
	}

	virtual void OnImGuiRender() override {
		ImGui::ShowDemoWindow();

		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
		if (ImGui::Begin("Window")) {
			const auto windowSize = ImGui::GetContentRegionAvail();
			const auto winSize    = glm::uvec2(windowSize.x, windowSize.y);
			if (winSize != _sceneSize) { _swapchainDirty = true; }
			_sceneSize = winSize;

			auto& main     = _renderGraph->GetTextureResource(_uiInput);
			auto& mainView = _renderGraph->GetPhysicalTextureResource(main.GetPhysicalIndex());
			auto mainTex   = GetImGui().Texture(mainView);
			ImGui::Image(ImTextureID(mainTex), windowSize);
		}
		ImGui::End();
		ImGui::PopStyleVar();
	}

 private:
	struct DefaultImages {
		Luna::Vulkan::ImageHandle Black2D;
		Luna::Vulkan::ImageHandle BlackCube;
		Luna::Vulkan::ImageHandle Gray2D;
		Luna::Vulkan::ImageHandle Normal2D;
		Luna::Vulkan::ImageHandle White2D;
		Luna::Vulkan::ImageHandle WhiteCube;
	};

	void BakeRenderGraph() {
		auto physicalBuffers = _renderGraph->ConsumePhysicalBuffers();

		_renderGraph->Reset();
		GetDevice().NextFrame();  // Release old Render Graph resources.

		const Luna::ResourceDimensions backbufferDims{.Format = _swapchainConfig.Format.format,
		                                              .Width  = _swapchainConfig.Extent.width,
		                                              .Height = _swapchainConfig.Extent.height};
		_renderGraph->SetBackbufferDimensions(backbufferDims);

#if 1
		auto& mainPass = _renderGraph->AddPass("Main", Luna::RenderGraphQueueFlagBits::Graphics);
		Luna::AttachmentInfo mainColor;
		Luna::AttachmentInfo mainDepth = {.SizeClass        = Luna::SizeClass::InputRelative,
		                                  .Format           = GetDevice().GetDefaultDepthFormat(),
		                                  .SizeRelativeName = "Main-Color"};
		mainPass.AddColorOutput("Main-Color", mainColor);
		mainPass.SetDepthStencilOutput("Main-Depth", mainDepth);
		mainPass.SetGetClearColor([](uint32_t, vk::ClearColorValue* value) {
			if (value) { *value = vk::ClearColorValue(0.36f, 0.0f, 0.63f, 1.0f); }
			return true;
		});
		mainPass.SetGetClearDepthStencil([](vk::ClearDepthStencilValue* value) {
			if (value) { *value = vk::ClearDepthStencilValue(1.0f, 0); }
			return true;
		});
		mainPass.SetBuildRenderPass([&](Luna::Vulkan::CommandBuffer& cmd) { RenderSceneForward(cmd); });

		_renderGraph->SetBackbufferSource("Main-Color");
#else
		const Luna::AttachmentInfo baseAttachment = {
			.SizeClass = Luna::SizeClass::Absolute, .Width = float(_sceneSize.x), .Height = float(_sceneSize.y)};

		auto& gBufferPass = _renderGraph->AddPass("G-Buffer", Luna::RenderGraphQueueFlagBits::Graphics);
		auto albedo       = baseAttachment.Copy().SetFormat(vk::Format::eR8G8B8A8Srgb);
		auto normal       = baseAttachment.Copy().SetFormat(vk::Format::eR16G16B16A16Unorm);
		auto orm          = baseAttachment.Copy().SetFormat(vk::Format::eR16G16B16A16Unorm);
		auto emissive     = baseAttachment.Copy().SetFormat(vk::Format::eR8G8B8A8Srgb);
		auto depth        = baseAttachment.Copy().SetFormat(GetDevice().GetDefaultDepthFormat());
		gBufferPass.AddColorOutput("G-Albedo", albedo);
		gBufferPass.AddColorOutput("G-Normal", normal);
		gBufferPass.AddColorOutput("G-ORM", orm);
		gBufferPass.AddColorOutput("G-Emissive", emissive);
		gBufferPass.SetDepthStencilOutput("G-Depth", depth);
		gBufferPass.SetGetClearColor([](uint32_t index, vk::ClearColorValue* value) {
			if (index == 0 || index == 1 || index == 2 || index == 3) {
				if (value) { *value = vk::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f); }
				return true;
			}
			return false;
		});
		gBufferPass.SetGetClearDepthStencil([](vk::ClearDepthStencilValue* value) {
			if (value) { *value = vk::ClearDepthStencilValue(1.0f, 0); }
			return true;
		});
		gBufferPass.SetBuildRenderPass([&](Luna::Vulkan::CommandBuffer& cmd) { RenderSceneGBuffer(cmd); });

		auto& lightingPass = _renderGraph->AddPass("Lighting", Luna::RenderGraphQueueFlagBits::Graphics);
		auto sceneOut      = baseAttachment;
		auto lit           = baseAttachment;
		lightingPass.AddAttachmentInput("G-Albedo");
		lightingPass.AddAttachmentInput("G-Normal");
		lightingPass.AddAttachmentInput("G-ORM");
		lightingPass.AddAttachmentInput("G-Emissive");
		lightingPass.AddColorOutput("Lit", lit);
		lightingPass.SetBuildRenderPass([&](Luna::Vulkan::CommandBuffer& cmd) { RenderSceneDeferred(cmd); });

		_uiInput = "Lit";

		auto& uiPass = _renderGraph->AddPass("UI", Luna::RenderGraphQueueFlagBits::Graphics);
		Luna::AttachmentInfo uiColor;
		uiPass.AddTextureInput(_uiInput);
		uiPass.AddColorOutput("UI-Color", uiColor);
		uiPass.SetGetClearColor([](uint32_t, vk::ClearColorValue* value) {
			if (value) { *value = vk::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f); }
			return true;
		});
		uiPass.SetBuildRenderPass([&](Luna::Vulkan::CommandBuffer& cmd) {
			const auto& uiColor = _renderGraph->GetTextureResource("UI-Color");
			const auto& uiDim   = _renderGraph->GetResourceDimensions(uiColor);
			GetImGui().BeginFrame(vk::Extent2D(uiDim.Width, uiDim.Height));
			OnImGuiRender();
			GetImGui().Render(cmd, false);
		});

		_renderGraph->SetBackbufferSource("UI-Color");
#endif

		_renderGraph->Bake();
		_renderGraph->InstallPhysicalBuffers(physicalBuffers);

		_renderGraph->Log();
	}

	void LoadShaders() {
		auto& device = GetDevice();
		/*
		_program =
		  device.RequestProgram(ReadFile("Resources/Shaders/PBR.vert.glsl"), ReadFile("Resources/Shaders/PBR.frag.glsl"));
		_programSkybox   = device.RequestProgram(ReadFile("Resources/Shaders/Skybox.vert.glsl"),
		                                       ReadFile("Resources/Shaders/Skybox.frag.glsl"));
		_programGBuffer  = device.RequestProgram(ReadFile("Resources/Shaders/PBR.vert.glsl"),
		                                        ReadFile("Resources/Shaders/GBuffer.frag.glsl"));
		_programDeferred = device.RequestProgram(ReadFile("Resources/Shaders/Fullscreen.vert.glsl"),
		                                         ReadFile("Resources/Shaders/Deferred.frag.glsl"));
		                                         */
	}

	void RenderSceneForward(Luna::Vulkan::CommandBuffer& cmd) {
		auto& device              = GetDevice();
		const auto frameIndex     = device.GetFrameIndex();
		const glm::uvec2 fbSize   = _sceneSize;
		PushConstant pushConstant = {};

		_sceneUBO->Bind(cmd, 0, 0);

		const auto SetTexture = [&](uint32_t set, uint32_t binding, Texture& texture, Luna::Vulkan::ImageHandle& fallback) {
			if (texture.Image) {
				cmd.SetTexture(set, binding, texture.Image->Image->GetView(), texture.Sampler->Sampler);
			} else {
				cmd.SetTexture(set, binding, fallback->GetView(), Luna::Vulkan::StockSampler::NearestWrap);
			}
		};

		cmd.SetTexture(0,
		               1,
		               _environment ? _environment->Irradiance->GetView() : _defaultImages.BlackCube->GetView(),
		               Luna::Vulkan::StockSampler::LinearClamp);
		cmd.SetTexture(0,
		               2,
		               _environment ? _environment->Prefiltered->GetView() : _defaultImages.BlackCube->GetView(),
		               Luna::Vulkan::StockSampler::LinearClamp);
		cmd.SetTexture(0,
		               3,
		               _environment ? _environment->BrdfLut->GetView() : _defaultImages.Black2D->GetView(),
		               Luna::Vulkan::StockSampler::LinearClamp);

		std::function<void(Model&, const Node*)> IterateNode = [&](Model& model, const Node* node) {
			if (node->Mesh) {
				const auto mesh   = node->Mesh;
				pushConstant.Node = node->GetGlobalTransform();

				cmd.SetVertexBinding(0, *mesh->Buffer, 0, sizeof(Vertex), vk::VertexInputRate::eVertex);
				if (mesh->TotalIndexCount > 0) { cmd.SetIndexBuffer(*mesh->Buffer, mesh->IndexOffset, vk::IndexType::eUint32); }

				const size_t submeshCount = mesh->Submeshes.size();
				for (size_t i = 0; i < submeshCount; ++i) {
					const auto& submesh  = mesh->Submeshes[i];
					const auto* material = submesh.Material;
					material->Update(device);
					cmd.PushConstants(&pushConstant, 0, sizeof(PushConstant));

					cmd.SetUniformBuffer(1, 0, *material->DataBuffer);
					SetTexture(1, 1, *material->Albedo, _defaultImages.White2D);
					SetTexture(1, 2, *material->Normal, _defaultImages.Normal2D);
					SetTexture(1, 3, *material->PBR, _defaultImages.White2D);
					SetTexture(1, 4, *material->Occlusion, _defaultImages.White2D);
					SetTexture(1, 5, *material->Emissive, _defaultImages.Black2D);

					if (submesh.IndexCount == 0) {
						cmd.Draw(submesh.VertexCount, 1, submesh.FirstVertex, 0);
					} else {
						cmd.DrawIndexed(submesh.IndexCount, 1, submesh.FirstIndex, submesh.FirstVertex, 0);
					}
				}
			}

			for (const auto* child : node->Children) { IterateNode(model, child); }
		};

		auto RenderModel = [&](Model& model) {
			LunaCmdZone(cmd, "Render Model");

			cmd.SetProgram(_program);
			cmd.SetVertexAttribute(0, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, Position));
			cmd.SetVertexAttribute(1, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, Normal));
			cmd.SetVertexAttribute(2, 0, vk::Format::eR32G32B32A32Sfloat, offsetof(Vertex, Tangent));
			cmd.SetVertexAttribute(3, 0, vk::Format::eR32G32Sfloat, offsetof(Vertex, Texcoord0));
			cmd.SetVertexAttribute(4, 0, vk::Format::eR32G32Sfloat, offsetof(Vertex, Texcoord1));
			cmd.SetVertexAttribute(5, 0, vk::Format::eR32G32B32A32Sfloat, offsetof(Vertex, Color0));
			cmd.SetVertexAttribute(6, 0, vk::Format::eR32G32B32A32Uint, offsetof(Vertex, Joints0));
			cmd.SetVertexAttribute(7, 0, vk::Format::eR32G32B32A32Sfloat, offsetof(Vertex, Weights0));

			for (const auto* node : model.RootNodes) { IterateNode(model, node); }
		};
		if (_model) { RenderModel(*_model); }

		if (_environment) {
			LunaCmdZone(cmd, "Render Skybox");

			cmd.SetOpaqueState();
			cmd.SetProgram(_programSkybox);
			cmd.SetDepthCompareOp(vk::CompareOp::eLessOrEqual);
			cmd.SetDepthWrite(false);
			cmd.SetCullMode(vk::CullModeFlagBits::eFront);
			_sceneUBO->Bind(cmd, 0, 0);
			cmd.SetTexture(1, 0, _environment->Skybox->GetView(), Luna::Vulkan::StockSampler::LinearClamp);
			cmd.Draw(36);
		}
	}

	void RenderSceneDeferred(Luna::Vulkan::CommandBuffer& cmd) {
		auto& device              = GetDevice();
		PushConstant pushConstant = {};

		_sceneUBO->Bind(cmd, 0, 0);

		const auto SetTexture = [&](uint32_t set, uint32_t binding, Texture& texture, Luna::Vulkan::ImageHandle& fallback) {
			if (texture.Image) {
				cmd.SetTexture(set, binding, texture.Image->Image->GetView(), texture.Sampler->Sampler);
			} else {
				cmd.SetTexture(set, binding, fallback->GetView(), Luna::Vulkan::StockSampler::NearestWrap);
			}
		};

		cmd.SetTexture(0,
		               1,
		               _environment ? _environment->Irradiance->GetView() : _defaultImages.BlackCube->GetView(),
		               Luna::Vulkan::StockSampler::LinearClamp);
		cmd.SetTexture(0,
		               2,
		               _environment ? _environment->Prefiltered->GetView() : _defaultImages.BlackCube->GetView(),
		               Luna::Vulkan::StockSampler::LinearClamp);
		cmd.SetTexture(0,
		               3,
		               _environment ? _environment->BrdfLut->GetView() : _defaultImages.Black2D->GetView(),
		               Luna::Vulkan::StockSampler::LinearClamp);

		cmd.SetProgram(_programDeferred);
		cmd.Draw(3);
	}

	void RenderSceneGBuffer(Luna::Vulkan::CommandBuffer& cmd) {
		auto& device              = GetDevice();
		PushConstant pushConstant = {};

		_sceneUBO->Bind(cmd, 0, 0);

		const auto SetTexture = [&](uint32_t set, uint32_t binding, Texture& texture, Luna::Vulkan::ImageHandle& fallback) {
			if (texture.Image) {
				cmd.SetTexture(set, binding, texture.Image->Image->GetView(), texture.Sampler->Sampler);
			} else {
				cmd.SetTexture(set, binding, fallback->GetView(), Luna::Vulkan::StockSampler::NearestWrap);
			}
		};

		std::function<void(Model&, const Node*)> IterateNode = [&](Model& model, const Node* node) {
			if (node->Mesh) {
				const auto mesh   = node->Mesh;
				pushConstant.Node = node->GetGlobalTransform();

				cmd.SetVertexBinding(0, *mesh->Buffer, 0, sizeof(Vertex), vk::VertexInputRate::eVertex);
				if (mesh->TotalIndexCount > 0) { cmd.SetIndexBuffer(*mesh->Buffer, mesh->IndexOffset, vk::IndexType::eUint32); }

				const size_t submeshCount = mesh->Submeshes.size();
				for (size_t i = 0; i < submeshCount; ++i) {
					const auto& submesh  = mesh->Submeshes[i];
					const auto* material = submesh.Material;
					material->Update(device);
					cmd.PushConstants(&pushConstant, 0, sizeof(PushConstant));

					cmd.SetUniformBuffer(1, 0, *material->DataBuffer);
					SetTexture(1, 1, *material->Albedo, _defaultImages.White2D);
					SetTexture(1, 2, *material->Normal, _defaultImages.Normal2D);
					SetTexture(1, 3, *material->PBR, _defaultImages.White2D);
					SetTexture(1, 4, *material->Occlusion, _defaultImages.White2D);
					SetTexture(1, 5, *material->Emissive, _defaultImages.Black2D);

					if (submesh.IndexCount == 0) {
						cmd.Draw(submesh.VertexCount, 1, submesh.FirstVertex, 0);
					} else {
						cmd.DrawIndexed(submesh.IndexCount, 1, submesh.FirstIndex, submesh.FirstVertex, 0);
					}
				}
			}

			for (const auto* child : node->Children) { IterateNode(model, child); }
		};

		auto RenderModel = [&](Model& model) {
			LunaCmdZone(cmd, "Render Model");

			cmd.SetProgram(_programGBuffer);
			cmd.SetVertexAttribute(0, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, Position));
			cmd.SetVertexAttribute(1, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, Normal));
			cmd.SetVertexAttribute(2, 0, vk::Format::eR32G32B32A32Sfloat, offsetof(Vertex, Tangent));
			cmd.SetVertexAttribute(3, 0, vk::Format::eR32G32Sfloat, offsetof(Vertex, Texcoord0));
			cmd.SetVertexAttribute(4, 0, vk::Format::eR32G32Sfloat, offsetof(Vertex, Texcoord1));
			cmd.SetVertexAttribute(5, 0, vk::Format::eR32G32B32A32Sfloat, offsetof(Vertex, Color0));
			cmd.SetVertexAttribute(6, 0, vk::Format::eR32G32B32A32Uint, offsetof(Vertex, Joints0));
			cmd.SetVertexAttribute(7, 0, vk::Format::eR32G32B32A32Sfloat, offsetof(Vertex, Weights0));

			for (const auto* node : model.RootNodes) { IterateNode(model, node); }
		};
		if (_model) { RenderModel(*_model); }
	}

	void StyleImGui() {
		ImGuiIO& io = ImGui::GetIO();

		io.ConfigWindowsMoveFromTitleBarOnly = true;

		// Style
		{
			auto& style = ImGui::GetStyle();

			// Main
			style.WindowPadding = ImVec2(8.0f, 8.0f);
			style.FramePadding  = ImVec2(5.0f, 3.0f);
			style.CellPadding   = ImVec2(4.0f, 2.0f);

			// Rounding
			style.WindowRounding    = 8.0f;
			style.ChildRounding     = 8.0f;
			style.FrameRounding     = 8.0f;
			style.PopupRounding     = 2.0f;
			style.ScrollbarRounding = 12.0f;
			style.GrabRounding      = 0.0f;
			style.LogSliderDeadzone = 4.0f;
			style.TabRounding       = 4.0f;
		}

		// Fonts
		{
			io.Fonts->Clear();

			io.Fonts->AddFontFromFileTTF("Resources/Fonts/Roboto-SemiMedium.ttf", 16.0f);

			ImFontConfig jpConfig;
			jpConfig.MergeMode = true;
			io.Fonts->AddFontFromFileTTF(
				"Resources/Fonts/NotoSansJP-Medium.otf", 18.0f, &jpConfig, io.Fonts->GetGlyphRangesJapanese());

			ImFontConfig faConfig;
			faConfig.MergeMode                 = true;
			faConfig.PixelSnapH                = true;
			static const ImWchar fontAwesome[] = {ICON_MIN_FA, ICON_MAX_16_FA, 0};
			io.Fonts->AddFontFromFileTTF("Resources/Fonts/FontAwesome6Free-Regular-400.otf", 16.0f, &faConfig, fontAwesome);
			io.Fonts->AddFontFromFileTTF("Resources/Fonts/FontAwesome6Free-Solid-900.otf", 16.0f, &faConfig, fontAwesome);
		}

		UpdateImGuiFontAtlas();
	}

	std::unique_ptr<Luna::RenderGraph> _renderGraph;
	Luna::Vulkan::SwapchainConfiguration _swapchainConfig;
	bool _swapchainDirty = true;

	Luna::Vulkan::Program* _program         = nullptr;
	Luna::Vulkan::Program* _programSkybox   = nullptr;
	Luna::Vulkan::Program* _programGBuffer  = nullptr;
	Luna::Vulkan::Program* _programDeferred = nullptr;
	std::unique_ptr<Environment> _environment;
	std::unique_ptr<Model> _model;
	std::unique_ptr<UniformBufferSet<SceneUBO>> _sceneUBO;
	DefaultImages _defaultImages;

	std::string _uiInput;
	glm::uvec2 _sceneSize = glm::uvec2(512, 512);

	Luna::Vulkan::BufferHandle _blasBuffer;
	vk::AccelerationStructureKHR _blas;
	vk::DeviceAddress _blasAddress;
	Luna::Vulkan::BufferHandle _tlasBuffer;
	vk::AccelerationStructureKHR _tlas;
	vk::DeviceAddress _tlasAddress;
	Luna::Vulkan::ImageHandle _rtImage;
	Luna::Vulkan::BufferHandle _rtUniform;
	vk::DescriptorSetLayout _rtSetLayout;
	vk::DescriptorPool _rtPool;
	vk::DescriptorSet _rtSet;
	vk::PipelineLayout _rtPipelineLayout;
	vk::Pipeline _rtPipeline;
	Luna::Vulkan::BufferHandle _rgenTable;
	Luna::Vulkan::BufferHandle _missTable;
	Luna::Vulkan::BufferHandle _chitTable;
};

Luna::Application* Luna::CreateApplication(int argc, const char** argv) {
	return new ViewerApplication();
}
