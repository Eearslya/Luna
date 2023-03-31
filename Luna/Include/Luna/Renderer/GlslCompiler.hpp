#pragma once

#include <Luna/Utility/Path.hpp>
#include <Luna/Vulkan/Common.hpp>

namespace Luna {
class GlslCompiler {
 public:
	const std::vector<Path>& GetDependencies() const {
		return _dependencies;
	}
	Hash GetSourceHash() const {
		return _sourceHash;
	}

	std::vector<uint32_t> Compile(std::string& error, const std::vector<std::pair<std::string, int>>& defines = {});
	bool Preprocess();
	void SetIncludeDirectories(const std::vector<Path>& includeDirs);
	void SetSourceFromFile(const Path& path, Vulkan::ShaderStage stage);

 private:
	bool Parse(const Path& sourcePath, const std::string& source);
	std::pair<Path, std::string> ResolveInclude(const Path& sourcePath, const std::string& includePath);

	std::string _source;
	Path _sourcePath;
	Hash _sourceHash;
	Vulkan::ShaderStage _stage;
	std::vector<Path> _includeDirs;

	std::vector<Path> _dependencies;
	std::string _processedSource;
};
}  // namespace Luna
