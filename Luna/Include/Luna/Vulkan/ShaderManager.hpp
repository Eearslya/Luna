#pragma once

#include <Luna/Renderer/GlslCompiler.hpp>
#include <Luna/Utility/Path.hpp>
#include <Luna/Vulkan/Common.hpp>
#include <Luna/Vulkan/Shader.hpp>
#include <filesystem>

namespace Luna {
namespace Vulkan {
struct PrecomputedMeta : IntrusiveHashMapEnabled<PrecomputedMeta> {
	PrecomputedMeta(Hash srcHash, Hash shaderHash) : SourceHash(srcHash), ShaderHash(shaderHash) {}

	Hash SourceHash;
	Hash ShaderHash;
};
using PrecomputedShaderCache = VulkanCache<PrecomputedMeta>;
using ReflectionCache        = VulkanCache<IntrusivePODWrapper<ShaderResourceLayout>>;

struct MetaCache {
	PrecomputedShaderCache VariantToShader;
	ReflectionCache ShaderToLayout;
};

struct ShaderTemplateVariant : public IntrusiveHashMapEnabled<ShaderTemplateVariant> {
	Hash VariantHash = 0;
	Hash SpirvHash   = 0;
	std::vector<uint32_t> Spirv;
	std::vector<std::pair<std::string, int>> Defines;
	uint32_t Instance = 0;

	Shader* Resolve(Device& device) const;
};

class ShaderTemplate : public IntrusiveHashMapEnabled<ShaderTemplate> {
 public:
	ShaderTemplate(Device& device,
	               const Path& shaderPath,
	               ShaderStage stage,
	               MetaCache& cache,
	               Hash pathHash,
	               const std::vector<Path>& includeDirs);
	~ShaderTemplate() noexcept;

	Hash GetPathHash() const {
		return _pathHash;
	}

	const ShaderTemplateVariant* RegisterVariant(const std::vector<std::pair<std::string, int>>& defines = {});

 private:
	void UpdateVariantCache(const ShaderTemplateVariant& variant);

	Device& _device;
	Path _path;
	Hash _pathHash = 0;
	ShaderStage _stage;
	MetaCache& _cache;
	VulkanCache<ShaderTemplateVariant> _variants;

	// Used when loading raw SPIR-V shaders.
	std::vector<uint32_t> _staticShader;

	// Used when loading shaders from GLSL source.
	std::unique_ptr<GlslCompiler> _compiler;
	std::vector<Path> _includeDirs;
	Hash _sourceHash = 0;
};

class ShaderProgramVariant : public IntrusiveHashMapEnabled<ShaderProgramVariant> {
	friend class ShaderProgram;

 public:
	ShaderProgramVariant(Device& device);

	Vulkan::Program* GetProgram();

 private:
	Vulkan::Program* GetCompute();
	Vulkan::Program* GetGraphics();

	Device& _device;
	std::array<const ShaderTemplateVariant*, ShaderStageCount> _stages;
};

class ShaderProgram : public IntrusiveHashMapEnabled<ShaderProgram> {
 public:
	ShaderProgram(Device& device, ShaderTemplate& compute);
	ShaderProgram(Device& device, ShaderTemplate& vertex, ShaderTemplate& fragment);

	ShaderProgramVariant* RegisterVariant(const std::vector<std::pair<std::string, int>>& defines);
	void SetStage(ShaderStage, ShaderTemplate* shader);

 private:
	Device& _device;
	std::array<ShaderTemplate*, ShaderStageCount> _stages;
	VulkanCacheReadWrite<ShaderProgramVariant> _variantCache;
};

class ShaderManager {
 public:
	ShaderManager(Device& device);
	ShaderManager(const ShaderManager&)  = delete;
	ShaderManager(ShaderManager&&)       = delete;
	void operator=(const ShaderManager&) = delete;
	void operator=(ShaderManager&&)      = delete;
	~ShaderManager() noexcept;

	ShaderProgram* RegisterCompute(const Path& compute);
	ShaderProgram* RegisterGraphics(const Path& vertex, const Path& fragment);

 private:
	ShaderTemplate* GetTemplate(const Path& path, ShaderStage stage);

	Device& _device;
	MetaCache _metaCache;
	VulkanCache<ShaderTemplate> _shaders;
	VulkanCache<ShaderProgram> _programs;
	std::vector<Path> _includeDirs;
};
}  // namespace Vulkan
}  // namespace Luna
