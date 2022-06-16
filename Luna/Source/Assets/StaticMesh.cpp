#include <Luna/Assets/AssetManager.hpp>
#include <Luna/Assets/StaticMesh.hpp>

namespace Luna {
void StaticMeshDeleter::operator()(StaticMesh* mesh) {
	auto manager = AssetManager::Get();
	manager->FreeStaticMesh(mesh);
}

StaticMesh::StaticMesh() {}

StaticMesh::~StaticMesh() noexcept {}
}  // namespace Luna
