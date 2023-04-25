#pragma once

#include <glm/glm.hpp>

namespace Luna {
class RenderContext {
 public:
	const glm::mat4& Projection() const {
		return _projection;
	}
	const glm::mat4& View() const {
		return _view;
	}

	void SetCamera(const glm::mat4& projection, const glm::mat4& view) {
		_projection = projection;
		_view       = view;
	}

 private:
	glm::mat4 _projection;
	glm::mat4 _view;
};
}  // namespace Luna
