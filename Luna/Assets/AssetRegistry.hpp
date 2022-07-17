#pragma once

#include <unordered_map>

#include "AssetMetadata.hpp"

namespace Luna {
class AssetRegistry {
 public:
	AssetMetadata& operator[](const AssetHandle handle);
	const AssetMetadata& Get(const AssetHandle handle) const;

	bool Contains(const AssetHandle handle) const;
	size_t Count() const {
		return _registry.size();
	}

	void Clear();
	size_t Remove(const AssetHandle handle);

	auto begin() {
		return _registry.begin();
	}
	auto end() {
		return _registry.end();
	}
	auto cbegin() {
		return _registry.cbegin();
	}
	auto cend() {
		return _registry.cend();
	}

 private:
	std::unordered_map<AssetHandle, AssetMetadata> _registry;
};
}  // namespace Luna
