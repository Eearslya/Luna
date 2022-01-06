#include <Luna/Assets/StaticMesh.hpp>
#include <Luna/Graphics/AssetManager.hpp>
#include <Luna/Graphics/Graphics.hpp>

namespace Luna {
void StaticMeshDeleter::operator()(StaticMesh* mesh) {
	auto graphics = Graphics::Get();
	auto& manager = graphics->GetAssetManager();
	manager.FreeStaticMesh(mesh);
}

StaticMesh::StaticMesh() {}

StaticMesh::~StaticMesh() noexcept {}
}  // namespace Luna
