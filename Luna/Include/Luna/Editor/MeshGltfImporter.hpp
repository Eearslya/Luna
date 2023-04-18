#pragma once

namespace Luna {
class Path;

class MeshGltfImporter {
 public:
	static bool Import(const Path& assetPath);
};
}  // namespace Luna
