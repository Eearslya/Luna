#pragma once

#include <Luna/Renderer/RenderParameters.hpp>

namespace Luna {
class RenderContext {
 public:
	const glm::mat4& InverseProjection() const {
		return _invProjection;
	}
	const glm::mat4& InverseView() const {
		return _invView;
	}
	const glm::mat4& InverseViewProjection() const {
		return _invViewProjection;
	}
	const glm::mat4& Projection() const {
		return _projection;
	}
	const glm::mat4& View() const {
		return _view;
	}
	const glm::mat4& ViewProjection() const {
		return _viewProjection;
	}
	const glm::vec3& Position() const {
		return _position;
	}
	float ZNear() const {
		return _zNear;
	}
	float ZFar() const {
		return _zFar;
	}

	CameraParameters GetCamera() const {
		return CameraParameters{.ViewProjection    = _viewProjection,
		                        .InvViewProjection = _invViewProjection,
		                        .Projection        = _projection,
		                        .InvProjection     = _invProjection,
		                        .View              = _view,
		                        .InvView           = _invView,
		                        .CameraPosition    = _position,
		                        .ZNear             = _zNear,
		                        .ZFar              = _zFar};
	}

	void SetCamera(const glm::mat4& projection, const glm::mat4& view) {
		_projection = projection;
		_view       = view;

		_viewProjection    = _projection * _view;
		_invProjection     = glm::inverse(_projection);
		_invView           = glm::inverse(_view);
		_invViewProjection = glm::inverse(_viewProjection);
		_position          = glm::vec3(_invView[3]);

		const glm::mat2 invZW(glm::vec2(_invProjection[2].z, _invProjection[2].w),
		                      glm::vec2(_invProjection[3].z, _invProjection[3].w));
		const auto Project = [](const glm::vec2& zw) -> float { return -zw.x / zw.y; };
		_zNear             = Project(invZW * glm::vec2(1, 1));
		_zFar              = Project(invZW * glm::vec2(0, 1));

		auto near = _invProjection * glm::vec4(0, 0, 1, 1);
		near /= near.w;
		_zNear = -near.z;
	}

 private:
	glm::mat4 _invProjection;
	glm::mat4 _invView;
	glm::mat4 _invViewProjection;
	glm::mat4 _projection;
	glm::mat4 _view;
	glm::mat4 _viewProjection;
	glm::vec3 _position;
	float _zNear;
	float _zFar;
};
}  // namespace Luna
