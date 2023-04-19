#pragma once

#include <Luna/Utility/Frustum.hpp>
#include <Luna/Vulkan/CommandBuffer.hpp>
#include <Luna/Vulkan/Common.hpp>
#include <Luna/Vulkan/DescriptorSet.hpp>
#include <glm/glm.hpp>

namespace Luna {
struct RenderParameters {
	glm::mat4 Projection;
	glm::mat4 View;
	glm::mat4 ViewProjection;
	glm::mat4 InvProjection;
	glm::mat4 InvView;
	glm::mat4 InvViewProjection;
	glm::mat4 LocalViewProjection;
	glm::mat4 InvLocalViewProjection;

	alignas(16) glm::vec3 CameraPosition;
	alignas(16) glm::vec3 CameraFront;
	alignas(16) glm::vec3 CameraRight;
	alignas(16) glm::vec3 CameraUp;

	float ZNear;
	float ZFar;
};

class RenderContext {
 public:
	struct DefaultImages {
		Vulkan::ImageHandle Black2D;
		Vulkan::ImageHandle Gray2D;
		Vulkan::ImageHandle Normal2D;
		Vulkan::ImageHandle White2D;
	};

	RenderContext(Vulkan::Device& device);

	vk::DescriptorSet GetBindlessSet() const {
		return _bindless[_frameIndex]->GetDescriptorSet();
	}
	const DefaultImages& GetDefaultImages() const {
		return _defaultImages;
	}
	Vulkan::Device& GetDevice() {
		return _device;
	}
	const Vulkan::Device& GetDevice() const {
		return _device;
	}
	const uint32_t GetFrameContextCount() const;
	const uint32_t GetFrameIndex() const {
		return _frameIndex;
	}
	const Frustum& GetFrustum() const {
		return _frustum;
	}
	const RenderParameters& GetRenderParameters() const {
		return _camera;
	}

	void BeginFrame(uint32_t frameIndex);
	void SetCamera(const glm::mat4& projection, const glm::mat4& view);
	uint32_t SetTexture(const Vulkan::ImageView& view, const Vulkan::Sampler& sampler) const;
	uint32_t SetTexture(const Vulkan::ImageView& view, Vulkan::StockSampler sampler) const;
	uint32_t SetSrgbTexture(const Vulkan::ImageView& view, const Vulkan::Sampler& sampler) const;
	uint32_t SetSrgbTexture(const Vulkan::ImageView& view, Vulkan::StockSampler sampler) const;
	uint32_t SetUnormTexture(const Vulkan::ImageView& view, const Vulkan::Sampler& sampler) const;
	uint32_t SetUnormTexture(const Vulkan::ImageView& view, Vulkan::StockSampler sampler) const;

 private:
	void CreateDefaultImages();

	Vulkan::Device& _device;

	mutable std::vector<std::unique_ptr<Vulkan::BindlessAllocator>> _bindless;
	RenderParameters _camera;
	Frustum _frustum;
	DefaultImages _defaultImages;
	uint32_t _frameIndex = 0;

	std::vector<Vulkan::ImageHandle> _bindlessImages;
};
}  // namespace Luna