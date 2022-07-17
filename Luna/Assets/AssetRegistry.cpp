#include "AssetRegistry.hpp"

namespace Luna {
AssetMetadata& AssetRegistry::operator[](const AssetHandle handle) {
	return _registry[handle];
}

const AssetMetadata& AssetRegistry::Get(const AssetHandle handle) const {
	return _registry.at(handle);
}

bool AssetRegistry::Contains(const AssetHandle handle) const {
	return _registry.find(handle) != _registry.end();
}

void AssetRegistry::Clear() {
	_registry.clear();
}

size_t AssetRegistry::Remove(const AssetHandle handle) {
	return _registry.erase(handle);
}
}  // namespace Luna
