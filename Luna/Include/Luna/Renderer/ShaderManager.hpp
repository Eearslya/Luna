#pragma once

#include <Luna/Platform/Filesystem.hpp>
#include <Luna/Renderer/GlslCompiler.hpp>
#include <Luna/Utility/Path.hpp>
#include <Luna/Vulkan/Common.hpp>
#include <Luna/Vulkan/Shader.hpp>

namespace Luna {
struct PrecomputedMeta : IntrusiveHashMapEnabled<PrecomputedMeta> {
	PrecomputedMeta(Hash srcHash, Hash shaderHash) : SourceHash(srcHash), ShaderHash(shaderHash) {}

	Hash SourceHash;
	Hash ShaderHash;
};
using PrecomputedShaderCache = Vulkan::VulkanCache<PrecomputedMeta>;
using ReflectionCache        = Vulkan::VulkanCache<IntrusivePODWrapper<Vulkan::ShaderResourceLayout>>;

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

	Vulkan::Shader* Resolve() const;
};

class ShaderTemplate : public IntrusiveHashMapEnabled<ShaderTemplate> {
 public:
	ShaderTemplate(const Path& shaderPath,
	               Vulkan::ShaderStage stage,
	               MetaCache& cache,
	               Hash pathHash,
	               const std::vector<Path>& includeDirs);
	~ShaderTemplate() noexcept;

	const Path& GetPath() const {
		return _path;
	}
	Hash GetPathHash() const {
		return _pathHash;
	}

	void Recompile();
	void RegisterDependencies();
	const ShaderTemplateVariant* RegisterVariant(const std::vector<std::pair<std::string, int>>& defines = {});

 private:
	void RecompileVariant(ShaderTemplateVariant& variant);
	void UpdateVariantCache(const ShaderTemplateVariant& variant);

	Path _path;
	Hash _pathHash = 0;
	Vulkan::ShaderStage _stage;
	MetaCache& _cache;
	Vulkan::VulkanCache<ShaderTemplateVariant> _variants;

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
	ShaderProgramVariant();

	Vulkan::Program* GetProgram();

 private:
	Vulkan::Program* GetCompute();
	Vulkan::Program* GetGraphics();

	RWSpinLock _instanceLock;
	std::atomic<Vulkan::Program*> _program;
	std::unique_ptr<Vulkan::ImmutableSamplerBank> _samplerBank;
	std::array<std::atomic_uint32_t, Vulkan::ShaderStageCount> _shaderInstance;
	std::array<const ShaderTemplateVariant*, Vulkan::ShaderStageCount> _stages;
};

class ShaderProgram : public IntrusiveHashMapEnabled<ShaderProgram> {
 public:
	ShaderProgram(ShaderTemplate& compute);
	ShaderProgram(ShaderTemplate& vertex, ShaderTemplate& fragment);

	ShaderProgramVariant* RegisterVariant(const std::vector<std::pair<std::string, int>>& defines = {});
	void SetStage(Vulkan::ShaderStage, ShaderTemplate* shader);

 private:
	std::array<ShaderTemplate*, Vulkan::ShaderStageCount> _stages;
	Vulkan::VulkanCacheReadWrite<ShaderProgramVariant> _variantCache;
};

class ShaderManager final {
 public:
	static bool Initialize();
	static void Shutdown();

	static bool GetShaderHashByVariantHash(Hash variantHash, Hash& shaderHash);
	static bool GetResourceLayoutByShaderHash(Hash shaderHash, Vulkan::ShaderResourceLayout& layout);

	static void AddIncludeDirectory(const Path& path);
	static Vulkan::Program* GetGraphics(const Path& vertex,
	                                    const Path& fragment,
	                                    const std::vector<std::pair<std::string, int>>& defines = {});
	static void PromoteReadWriteCachesToReadOnly();
	static ShaderProgram* RegisterCompute(const Path& compute);
	static void RegisterDependency(ShaderTemplate* shader, const Path& dependency);
	static void RegisterDependencyNoLock(ShaderTemplate* shader, const Path& dependency);
	static ShaderProgram* RegisterGraphics(const Path& vertex, const Path& fragment);

 private:
	static ShaderTemplate* GetTemplate(const Path& path, Vulkan::ShaderStage stage);
	static void Recompile(const FileNotifyInfo& info);
};
}  // namespace Luna
