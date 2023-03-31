#pragma once

#include <Luna/Platform/Filesystem.hpp>
#include <Luna/Renderer/GlslCompiler.hpp>
#include <Luna/Utility/Path.hpp>
#include <Luna/Vulkan/Common.hpp>
#include <Luna/Vulkan/Shader.hpp>
#include <unordered_map>
#include <unordered_set>

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

	void Recompile();
	void RegisterDependencies(ShaderManager& manager);
	const ShaderTemplateVariant* RegisterVariant(const std::vector<std::pair<std::string, int>>& defines = {});

 private:
	void RecompileVariant(ShaderTemplateVariant& variant);
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
	RWSpinLock _instanceLock;
	std::atomic<Vulkan::Program*> _program;
	std::unique_ptr<ImmutableSamplerBank> _samplerBank;
	std::array<std::atomic_uint32_t, ShaderStageCount> _shaderInstance;
	std::array<const ShaderTemplateVariant*, ShaderStageCount> _stages;
};

class ShaderProgram : public IntrusiveHashMapEnabled<ShaderProgram> {
 public:
	ShaderProgram(Device& device, ShaderTemplate& compute);
	ShaderProgram(Device& device, ShaderTemplate& vertex, ShaderTemplate& fragment);

	ShaderProgramVariant* RegisterVariant(const std::vector<std::pair<std::string, int>>& defines = {});
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

	bool GetShaderHashByVariantHash(Hash variantHash, Hash& shaderHash) const;
	bool GetResourceLayoutByShaderHash(Hash shaderHash, ShaderResourceLayout& layout) const;

	void AddIncludeDirectory(const Path& path);
	Program* GetGraphics(const Path& vertex,
	                     const Path& fragment,
	                     const std::vector<std::pair<std::string, int>>& defines = {});
	void PromoteReadWriteCachesToReadOnly();
	ShaderProgram* RegisterCompute(const Path& compute);
	void RegisterDependency(ShaderTemplate* shader, const Path& dependency);
	void RegisterDependencyNoLock(ShaderTemplate* shader, const Path& dependency);
	ShaderProgram* RegisterGraphics(const Path& vertex, const Path& fragment);

 private:
	struct Notify {
		FilesystemBackend* Backend = nullptr;
		FileNotifyHandle Handle    = {};
	};

	ShaderTemplate* GetTemplate(const Path& path, ShaderStage stage);
	void Recompile(const FileNotifyInfo& info);

	Device& _device;
	MetaCache _metaCache;
	VulkanCache<ShaderTemplate> _shaders;
	VulkanCache<ShaderProgram> _programs;
	std::vector<Path> _includeDirs;
	std::unordered_map<Path, std::unordered_set<ShaderTemplate*>> _dependees;
	std::mutex _dependencyLock;
	std::unordered_map<Path, Notify> _directoryWatches;
};
}  // namespace Vulkan
}  // namespace Luna
