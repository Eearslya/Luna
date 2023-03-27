#pragma once

#include <Luna/Vulkan/CommandBuffer.hpp>
#include <Luna/Vulkan/Common.hpp>
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

	struct Shaders {
		Vulkan::Program* PBRForward  = nullptr;
		Vulkan::Program* PBRGBuffer  = nullptr;
		Vulkan::Program* PBRDeferred = nullptr;
	};

	DefaultImages& GetDefaultImages() {
		return _defaultImages;
	}
	const DefaultImages& GetDefaultImages() const {
		return _defaultImages;
	}
	const RenderParameters& GetRenderParameters() const {
		return _camera;
	}
	Shaders& GetShaders() {
		return _shaders;
	}
	const Shaders& GetShaders() const {
		return _shaders;
	}

	void SetBindless(Vulkan::CommandBuffer& cmd, uint32_t set, uint32_t binding) {
		_bindlessImages = {_defaultImages.Black2D};
	}

	void SetCamera(const glm::mat4& projection, const glm::mat4& view) {
		_camera.Projection        = projection;
		_camera.View              = view;
		_camera.ViewProjection    = _camera.Projection * _camera.View;
		_camera.InvProjection     = glm::inverse(_camera.Projection);
		_camera.InvView           = glm::inverse(_camera.View);
		_camera.InvViewProjection = glm::inverse(_camera.ViewProjection);

		glm::mat4 localView            = view;
		localView[3][0]                = 0;
		localView[3][1]                = 0;
		localView[3][2]                = 0;
		_camera.LocalViewProjection    = _camera.Projection * localView;
		_camera.InvLocalViewProjection = glm::inverse(_camera.LocalViewProjection);

		_camera.CameraRight    = _camera.InvView[0];
		_camera.CameraUp       = _camera.InvView[1];
		_camera.CameraFront    = -_camera.InvView[2];
		_camera.CameraPosition = _camera.InvView[3];

		glm::mat2 invZW(glm::vec2(_camera.InvProjection[2].z, _camera.InvProjection[2].w),
		                glm::vec2(_camera.InvProjection[3].z, _camera.InvProjection[3].w));
		const auto Project = [](const glm::vec2& zw) -> float { return -zw.x / zw.y; };
		_camera.ZNear      = Project(invZW * glm::vec2(0.0f, 1.0f));
		_camera.ZFar       = Project(invZW * glm::vec2(1.0f, 1.0f));
	}

 private:
	RenderParameters _camera;
	DefaultImages _defaultImages;
	Shaders _shaders;

	std::vector<Vulkan::ImageHandle> _bindlessImages;
};
}  // namespace Luna
