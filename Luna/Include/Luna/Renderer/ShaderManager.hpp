#pragma once

#include <Luna/Renderer/Common.hpp>
#include <Luna/Utility/Path.hpp>

namespace Luna {
struct MetaCache;
class ShaderCompiler;

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
	               Hash pathHash,
	               const std::vector<Path>& includeDirs);
	~ShaderTemplate() noexcept;

	[[nodiscard]] const Path& GetPath() const noexcept {
		return _path;
	}
	[[nodiscard]] Hash GetPathHash() const noexcept {
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
	Vulkan::VulkanCache<ShaderTemplateVariant> _variants;

	// Used when loading raw SPIR-V shaders.
	std::vector<uint32_t> _staticShader;

	// Used when loading shaders from GLSL source.
	std::unique_ptr<ShaderCompiler> _compiler;
	std::vector<Path> _includeDirs;
	Hash _sourceHash = 0;
};

class ShaderProgramVariant : public IntrusiveHashMapEnabled<ShaderProgramVariant> {
	friend class ShaderProgram;

 public:
	ShaderProgramVariant();
	~ShaderProgramVariant() noexcept;

	Vulkan::Program* GetProgram();

 private:
	Vulkan::Program* GetCompute();
	Vulkan::Program* GetGraphics();

	RWSpinLock _instanceLock;
	std::atomic<Vulkan::Program*> _program;
	std::array<std::atomic_uint32_t, Vulkan::ShaderStageCount> _shaderInstance;
	std::array<const ShaderTemplateVariant*, Vulkan::ShaderStageCount> _stages;
};

class ShaderProgram : public IntrusiveHashMapEnabled<ShaderProgram> {
 public:
	ShaderProgram(ShaderTemplate& compute);
	ShaderProgram(ShaderTemplate& vertex, ShaderTemplate& fragment);
	~ShaderProgram() noexcept;

	ShaderProgramVariant* RegisterVariant(const std::vector<std::pair<std::string, int>>& defines = {});
	void SetStage(Vulkan::ShaderStage stage, ShaderTemplate* shader);

 private:
	std::array<ShaderTemplate*, Vulkan::ShaderStageCount> _stages;
	Vulkan::VulkanCacheReadWrite<ShaderProgramVariant> _variantCache;
};

class ShaderManager {
 public:
	static bool Initialize();
	static void Update();
	static void Shutdown();

	static Vulkan::Program* GetGraphics(const Path& vertex,
	                                    const Path& fragment,
	                                    const std::vector<std::pair<std::string, int>>& defines = {});
	static ShaderProgram* RegisterCompute(const Path& compute);
	static ShaderProgram* RegisterGraphics(const Path& vertex, const Path& fragment);
};
}  // namespace Luna
