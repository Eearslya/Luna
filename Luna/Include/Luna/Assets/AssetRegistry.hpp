#pragma once

#include <Luna/Assets/AssetMetadata.hpp>
#include <unordered_map>

namespace Luna {
class AssetRegistry {
 public:
	void Clear() {
		_registry.clear();
	}
	bool Contains(const AssetHandle handle) const {
		return _registry.find(handle) != _registry.end();
	}
	AssetMetadata& Get(const AssetHandle handle) {
		return _registry.at(handle);
	}
	const AssetMetadata& Get(const AssetHandle handle) const {
		return _registry.at(handle);
	}
	size_t Remove(const AssetHandle handle) {
		return _registry.erase(handle);
	}
	size_t Size() const {
		return _registry.size();
	}

	AssetMetadata& operator[](const AssetHandle handle) {
		return _registry[handle];
	}
	const AssetMetadata& operator[](const AssetHandle handle) const {
		return _registry.at(handle);
	}

	auto begin() {
		return _registry.begin();
	}
	auto end() {
		return _registry.end();
	}
	auto begin() const {
		return _registry.begin();
	}
	auto end() const {
		return _registry.end();
	}
	auto cbegin() const {
		return _registry.begin();
	}
	auto cend() const {
		return _registry.end();
	}

 private:
	std::unordered_map<AssetHandle, AssetMetadata> _registry;
};
}  // namespace Luna
