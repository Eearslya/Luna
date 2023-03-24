#pragma once

#include <Luna/Renderer/StaticMesh.hpp>

namespace Luna {
struct MeshRendererComponent {
	IntrusivePtr<StaticMesh> StaticMesh;
};
}  // namespace Luna
