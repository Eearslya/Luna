#include <Luna/Platform/Filesystem.hpp>
#include <Luna/Utility/Log.hpp>
#include <Luna/Vulkan/Device.hpp>
#include <Luna/Vulkan/Shader.hpp>
#include <Luna/Vulkan/ShaderManager.hpp>

namespace Luna {
namespace Vulkan {
Shader* ShaderTemplateVariant::Resolve(Device& device) const {
	if (Spirv.empty()) {
		return device.RequestShader(SpirvHash);
	} else {
		return device.RequestShader(Spirv.size() * sizeof(uint32_t), Spirv.data());
	}
}

ShaderTemplate::ShaderTemplate(Device& device,
                               const Path& shaderPath,
                               ShaderStage stage,
                               MetaCache& cache,
                               Hash pathHash,
                               const std::vector<Path>& includeDirs)
		: _device(device), _path(shaderPath), _stage(stage), _cache(cache), _pathHash(pathHash), _includeDirs(includeDirs) {
	auto filesystem = Filesystem::Get();

	const auto ext = _path.Extension();
	if (ext == "spv") {
		auto spvFile = filesystem->OpenReadOnlyMapping(_path);
		if (!spvFile) { throw std::runtime_error("[ShaderTemplate] Failed to load Shader SPV file."); }

		_staticShader = {spvFile->Data<uint32_t>(), spvFile->Data<uint32_t>() + (spvFile->GetSize() / sizeof(uint32_t))};
		_sourceHash   = 0;
	} else {
		_compiler = std::make_unique<GlslCompiler>();
		_compiler->SetSourceFromFile(_path, _stage);
		_compiler->SetIncludeDirectories(_includeDirs);
		if (!_compiler->Preprocess()) {
			Log::Error("ShaderManager", "Failed to preprocess {} shader!", VulkanEnumToString(_stage));
			throw std::runtime_error("[ShaderManager] Failed to preprocess shader source.");
		}
		_sourceHash = _compiler->GetSourceHash();
	}
}

const ShaderTemplateVariant* ShaderTemplate::RegisterVariant(const std::vector<std::pair<std::string, int>>& defines) {
	Hasher h;
	h(defines.size());
	for (const auto& def : defines) {
		h(def.first);
		h(def.second);
	}

	const auto hash = h.Get();
	h(_pathHash);
	const auto completeHash = h.Get();

	auto* ret = _variants.Find(hash);
	if (!ret) {
		auto* variant        = _variants.Allocate();
		variant->VariantHash = completeHash;

		auto* precompiledSpirv = _cache.VariantToShader.Find(completeHash);
		if (precompiledSpirv) {
			if (!_device.RequestShader(precompiledSpirv->ShaderHash)) {
				precompiledSpirv = nullptr;
			} else if (_sourceHash != precompiledSpirv->SourceHash) {
				precompiledSpirv = nullptr;
			}
		}

		if (!precompiledSpirv) {
			if (!_staticShader.empty()) {
				variant->Spirv = _staticShader;
				UpdateVariantCache(*variant);
			} else if (_compiler) {
				std::string error;
				variant->Spirv = _compiler->Compile(error, defines);

				if (variant->Spirv.empty()) {
					Log::Error("ShaderManager", "Failed to compile {} shader: {}", VulkanEnumToString(_stage), error);
					_variants.Free(variant);
					return nullptr;
				}

				UpdateVariantCache(*variant);
			} else {
				return nullptr;
			}
		} else {
			variant->SpirvHash = precompiledSpirv->ShaderHash;
		}

		variant->Instance++;
		variant->Defines = defines;

		ret = _variants.InsertYield(hash, variant);
	}

	return ret;
}

void ShaderTemplate::UpdateVariantCache(const ShaderTemplateVariant& variant) {
	if (variant.Spirv.empty()) { return; }

	Hasher h;
	h.Data(variant.Spirv.size() * sizeof(uint32_t), variant.Spirv.data());
	const auto shaderHash = h.Get();

	const auto layout =
		Shader::ReflectShaderResourceLayout(variant.Spirv.size() * sizeof(uint32_t), variant.Spirv.data());
	auto* varToShader = _cache.VariantToShader.Find(variant.VariantHash);
	if (varToShader) {
		varToShader->SourceHash = _sourceHash;
		varToShader->ShaderHash = shaderHash;
	} else {
		_cache.VariantToShader.EmplaceYield(variant.VariantHash, _sourceHash, shaderHash);
	}

	_cache.ShaderToLayout.EmplaceYield(shaderHash, layout);
}

ShaderTemplate::~ShaderTemplate() noexcept {}

ShaderProgramVariant::ShaderProgramVariant(Device& device) : _device(device) {
	std::fill(_stages.begin(), _stages.end(), nullptr);
}

Vulkan::Program* ShaderProgramVariant::GetProgram() {
	auto* vert = _stages[int(ShaderStage::Vertex)];
	auto* frag = _stages[int(ShaderStage::Fragment)];
	auto* comp = _stages[int(ShaderStage::Compute)];

	if (comp) {
		return GetCompute();
	} else if (vert && frag) {
		return GetGraphics();
	}

	return nullptr;
}

Vulkan::Program* ShaderProgramVariant::GetCompute() {
	return nullptr;
}

Vulkan::Program* ShaderProgramVariant::GetGraphics() {
	Vulkan::Program* ret = nullptr;
	auto* vert           = _stages[int(ShaderStage::Vertex)];
	auto* frag           = _stages[int(ShaderStage::Fragment)];

	ret = _device.RequestProgram(vert->Resolve(_device), frag->Resolve(_device));

	return ret;
}

ShaderProgram::ShaderProgram(Device& device, ShaderTemplate& compute) : _device(device) {
	std::fill(_stages.begin(), _stages.end(), nullptr);
	SetStage(Vulkan::ShaderStage::Compute, &compute);
}

ShaderProgram::ShaderProgram(Device& device, ShaderTemplate& vertex, ShaderTemplate& fragment) : _device(device) {
	std::fill(_stages.begin(), _stages.end(), nullptr);
	SetStage(Vulkan::ShaderStage::Vertex, &vertex);
	SetStage(Vulkan::ShaderStage::Fragment, &fragment);
}

ShaderProgramVariant* ShaderProgram::RegisterVariant(const std::vector<std::pair<std::string, int>>& defines) {
	Hasher h;
	h(defines.size());
	for (auto& def : defines) {
		h(def.first);
		h(def.second);
	}
	const auto hash = h.Get();

	ShaderProgramVariant* variant = _variantCache.Find(hash);
	if (variant) { return variant; }

	variant = _variantCache.Allocate(_device);
	for (uint32_t i = 0; i < ShaderStageCount; ++i) {
		if (_stages[i]) { variant->_stages[i] = _stages[i]->RegisterVariant(defines); }
	}

	variant->GetProgram();
	variant = _variantCache.InsertYield(hash, variant);

	return variant;
}

void ShaderProgram::SetStage(ShaderStage stage, ShaderTemplate* shader) {
	_stages[int(stage)] = shader;
}

ShaderManager::ShaderManager(Device& device) : _device(device) {}

ShaderManager::~ShaderManager() noexcept {};

ShaderProgram* ShaderManager::RegisterCompute(const Path& compute) {
	return nullptr;
}

ShaderProgram* ShaderManager::RegisterGraphics(const Path& vertex, const Path& fragment) {
	auto* vertexTemplate   = GetTemplate(vertex, ShaderStage::Vertex);
	auto* fragmentTemplate = GetTemplate(fragment, ShaderStage::Fragment);
	if (!vertexTemplate || !fragmentTemplate) { return nullptr; }

	Hasher h;
	h(vertexTemplate->GetPathHash());
	h(fragmentTemplate->GetPathHash());
	const auto hash = h.Get();

	auto* ret = _programs.Find(hash);
	if (!ret) { ret = _programs.EmplaceYield(hash, _device, *vertexTemplate, *fragmentTemplate); }

	return ret;
}

ShaderTemplate* ShaderManager::GetTemplate(const Path& path, ShaderStage stage) {
	Hasher h(path);
	const auto hash = h.Get();

	auto* ret = _shaders.Find(hash);
	if (!ret) {
		auto* shader = _shaders.Allocate(_device, path, stage, _metaCache, hash, _includeDirs);
		ret          = _shaders.InsertYield(hash, shader);
	}

	return ret;
}
}  // namespace Vulkan
}  // namespace Luna
