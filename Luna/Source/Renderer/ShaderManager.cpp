#include <Luna/Renderer/Renderer.hpp>
#include <Luna/Renderer/ShaderManager.hpp>
#include <Luna/Utility/Log.hpp>
#include <Luna/Vulkan/Device.hpp>
#include <Luna/Vulkan/Sampler.hpp>
#include <Luna/Vulkan/Shader.hpp>
#include <unordered_map>
#include <unordered_set>

namespace Luna {
struct Notify {
	FilesystemBackend* Backend = nullptr;
	FileNotifyHandle Handle    = {};
};

static struct ShaderManagerState {
	MetaCache MetaCache;
	Vulkan::VulkanCache<ShaderTemplate> Shaders;
	Vulkan::VulkanCache<ShaderProgram> Programs;
	std::vector<Path> IncludeDirs;
	std::unordered_map<Path, std::unordered_set<ShaderTemplate*>> Dependees;
	std::mutex DependencyLock;
	std::unordered_map<Path, Notify> DirectoryWatches;
} State;

Vulkan::Shader* ShaderTemplateVariant::Resolve() const {
	if (Spirv.empty()) {
		return Renderer::GetDevice().RequestShader(SpirvHash);
	} else {
		return Renderer::GetDevice().RequestShader(Spirv.size() * sizeof(uint32_t), Spirv.data());
	}
}

ShaderTemplate::ShaderTemplate(const Path& shaderPath,
                               Vulkan::ShaderStage stage,
                               MetaCache& cache,
                               Hash pathHash,
                               const std::vector<Path>& includeDirs)
		: _path(shaderPath), _pathHash(pathHash), _stage(stage), _cache(cache), _includeDirs(includeDirs) {
	const auto ext = _path.Extension();
	if (ext == "spv") {
		auto spvFile = Filesystem::OpenReadOnlyMapping(_path);
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

ShaderTemplate::~ShaderTemplate() noexcept {}

void ShaderTemplate::Recompile() {
	auto newCompiler = std::make_unique<GlslCompiler>();
	newCompiler->SetSourceFromFile(_path, _stage);
	newCompiler->SetIncludeDirectories(_includeDirs);
	if (!newCompiler->Preprocess()) {
		Log::Error("ShaderManager", "Failed to preprocess {} shader!", VulkanEnumToString(_stage));
		return;
	}

	_compiler   = std::move(newCompiler);
	_sourceHash = _compiler->GetSourceHash();

	for (auto& variant : _variants.GetReadOnly()) { RecompileVariant(variant); }
	for (auto& variant : _variants.GetReadWrite()) { RecompileVariant(variant); }
}

void ShaderTemplate::RegisterDependencies() {
	if (_compiler) {
		for (auto& dep : _compiler->GetDependencies()) { ShaderManager::RegisterDependencyNoLock(this, dep); }
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
			if (!Renderer::GetDevice().RequestShader(precompiledSpirv->ShaderHash)) {
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

void ShaderTemplate::RecompileVariant(ShaderTemplateVariant& variant) {
	std::string error;
	auto newSpv = _compiler->Compile(error, variant.Defines);
	if (newSpv.empty()) {
		Log::Error("ShaderManager", "Failed to recompile {} shader: {}", VulkanEnumToString(_stage), error);
		return;
	}

	variant.Spirv = std::move(newSpv);
	variant.Instance++;
	UpdateVariantCache(variant);
}

void ShaderTemplate::UpdateVariantCache(const ShaderTemplateVariant& variant) {
	if (variant.Spirv.empty()) { return; }

	Hasher h;
	h.Data(variant.Spirv.size() * sizeof(uint32_t), variant.Spirv.data());
	const auto shaderHash = h.Get();

	const auto layout =
		Vulkan::Shader::ReflectShaderResourceLayout(variant.Spirv.size() * sizeof(uint32_t), variant.Spirv.data());
	auto* varToShader = _cache.VariantToShader.Find(variant.VariantHash);
	if (varToShader) {
		varToShader->SourceHash = _sourceHash;
		varToShader->ShaderHash = shaderHash;
	} else {
		_cache.VariantToShader.EmplaceYield(variant.VariantHash, _sourceHash, shaderHash);
	}

	_cache.ShaderToLayout.EmplaceYield(shaderHash, layout);
}

ShaderProgramVariant::ShaderProgramVariant() {
	for (auto& inst : _shaderInstance) { inst.store(0, std::memory_order_relaxed); }
	_program.store(nullptr, std::memory_order_relaxed);
	std::fill(_stages.begin(), _stages.end(), nullptr);
}

Vulkan::Program* ShaderProgramVariant::GetProgram() {
	auto* vert = _stages[int(Vulkan::ShaderStage::Vertex)];
	auto* frag = _stages[int(Vulkan::ShaderStage::Fragment)];
	auto* comp = _stages[int(Vulkan::ShaderStage::Compute)];

	if (comp) {
		return GetCompute();
	} else if (vert && frag) {
		return GetGraphics();
	}

	return nullptr;
}

Vulkan::Program* ShaderProgramVariant::GetCompute() {
	Vulkan::Program* ret = nullptr;
	auto* comp           = _stages[int(Vulkan::ShaderStage::Compute)];

	ret = Renderer::GetDevice().RequestProgram(comp->Resolve());

	auto& compInstance      = _shaderInstance[int(Vulkan::ShaderStage::Compute)];
	uint32_t loadedInstance = compInstance.load(std::memory_order_acquire);
	if (loadedInstance == comp->Instance) { return _program.load(std::memory_order_relaxed); }

	_instanceLock.LockWrite();
	if (compInstance.load(std::memory_order_relaxed) != comp->Instance) {
		ret = Renderer::GetDevice().RequestProgram(comp->Resolve());
		_program.store(ret, std::memory_order_relaxed);
		compInstance.store(comp->Instance, std::memory_order_release);
	} else {
		ret = _program.load(std::memory_order_relaxed);
	}
	_instanceLock.UnlockWrite();

	return ret;
}

Vulkan::Program* ShaderProgramVariant::GetGraphics() {
	Vulkan::Program* ret = nullptr;
	auto* vert           = _stages[int(Vulkan::ShaderStage::Vertex)];
	auto* frag           = _stages[int(Vulkan::ShaderStage::Fragment)];

	ret = Renderer::GetDevice().RequestProgram(vert->Resolve(), frag->Resolve());

	auto& vertInstance          = _shaderInstance[int(Vulkan::ShaderStage::Vertex)];
	auto& fragInstance          = _shaderInstance[int(Vulkan::ShaderStage::Fragment)];
	uint32_t loadedVertInstance = vertInstance.load(std::memory_order_acquire);
	uint32_t loadedFragInstance = fragInstance.load(std::memory_order_acquire);
	if (loadedVertInstance == vert->Instance && loadedFragInstance == frag->Instance) {
		return _program.load(std::memory_order_relaxed);
	}

	_instanceLock.LockWrite();
	if (vertInstance.load(std::memory_order_relaxed) != vert->Instance ||
	    fragInstance.load(std::memory_order_relaxed) != frag->Instance) {
		ret = Renderer::GetDevice().RequestProgram(vert->Resolve(), frag->Resolve());
		_program.store(ret, std::memory_order_relaxed);
		vertInstance.store(vert->Instance, std::memory_order_release);
		fragInstance.store(frag->Instance, std::memory_order_release);
	} else {
		ret = _program.load(std::memory_order_relaxed);
	}
	_instanceLock.UnlockWrite();

	return ret;
}

ShaderProgram::ShaderProgram(ShaderTemplate& compute) {
	std::fill(_stages.begin(), _stages.end(), nullptr);
	SetStage(Vulkan::ShaderStage::Compute, &compute);
}

ShaderProgram::ShaderProgram(ShaderTemplate& vertex, ShaderTemplate& fragment) {
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

	variant = _variantCache.Allocate();
	for (uint32_t i = 0; i < Vulkan::ShaderStageCount; ++i) {
		if (_stages[i]) { variant->_stages[i] = _stages[i]->RegisterVariant(defines); }
	}

	variant->GetProgram();
	variant = _variantCache.InsertYield(hash, variant);

	return variant;
}

void ShaderProgram::SetStage(Vulkan::ShaderStage stage, ShaderTemplate* shader) {
	_stages[int(stage)] = shader;
}

bool ShaderManager::Initialize() {
	return true;
}

void ShaderManager::Update() {
	PromoteReadWriteCachesToReadOnly();
}

void ShaderManager::Shutdown() {
	for (auto& dir : State.DirectoryWatches) {
		if (dir.second.Backend) { dir.second.Backend->UnwatchFile(dir.second.Handle); }
	}
}

bool ShaderManager::GetShaderHashByVariantHash(Hash variantHash, Hash& shaderHash) {
	auto* shader = State.MetaCache.VariantToShader.Find(variantHash);
	if (shader) {
		shaderHash = shader->ShaderHash;
		return true;
	}

	return false;
}

bool ShaderManager::GetResourceLayoutByShaderHash(Hash shaderHash, Vulkan::ShaderResourceLayout& layout) {
	auto* shader = State.MetaCache.ShaderToLayout.Find(shaderHash);
	if (shader) {
		layout = shader->Value;
		return true;
	}

	return false;
}

void ShaderManager::AddIncludeDirectory(const Path& path) {
	if (std::find(State.IncludeDirs.begin(), State.IncludeDirs.end(), path) != State.IncludeDirs.end()) { return; }

	State.IncludeDirs.push_back(path);
}

Vulkan::Program* ShaderManager::GetGraphics(const Path& vertex,
                                            const Path& fragment,
                                            const std::vector<std::pair<std::string, int>>& defines) {
	auto* program = RegisterGraphics(vertex, fragment);
	if (!program) { return nullptr; }

	auto* variant = program->RegisterVariant(defines);
	if (!variant) { return nullptr; }

	return variant->GetProgram();
}

void ShaderManager::PromoteReadWriteCachesToReadOnly() {
	State.MetaCache.ShaderToLayout.MoveToReadOnly();
	State.MetaCache.VariantToShader.MoveToReadOnly();
	State.Programs.MoveToReadOnly();
	State.Shaders.MoveToReadOnly();
}

ShaderProgram* ShaderManager::RegisterCompute(const Path& compute) {
	auto* computeTemplate = GetTemplate(compute, Vulkan::ShaderStage::Compute);
	if (!computeTemplate) { return nullptr; }

	Hasher h;
	h(computeTemplate->GetPathHash());
	const auto hash = h.Get();

	auto* ret = State.Programs.Find(hash);
	if (!ret) { ret = State.Programs.EmplaceYield(hash, *computeTemplate); }

	return ret;
}

void ShaderManager::RegisterDependency(ShaderTemplate* shader, const Path& dependency) {
	std::lock_guard lock(State.DependencyLock);
	RegisterDependencyNoLock(shader, dependency);
}

void ShaderManager::RegisterDependencyNoLock(ShaderTemplate* shader, const Path& dependency) {
	State.Dependees[dependency].insert(shader);

	const auto baseDir = dependency.BaseDirectory();
	if (State.DirectoryWatches.find(baseDir) != State.DirectoryWatches.end()) { return; }

	auto [protocol, path] = baseDir.ProtocolSplit();
	auto* backend         = Filesystem::GetBackend(protocol);
	if (!backend) { return; }

	FileNotifyHandle handle = backend->WatchFile(path, [](const FileNotifyInfo& info) { Recompile(info); });
	if (handle >= 0) { State.DirectoryWatches[baseDir] = {backend, handle}; }
}

ShaderProgram* ShaderManager::RegisterGraphics(const Path& vertex, const Path& fragment) {
	auto* vertexTemplate   = GetTemplate(vertex, Vulkan::ShaderStage::Vertex);
	auto* fragmentTemplate = GetTemplate(fragment, Vulkan::ShaderStage::Fragment);
	if (!vertexTemplate || !fragmentTemplate) { return nullptr; }

	Hasher h;
	h(vertexTemplate->GetPathHash());
	h(fragmentTemplate->GetPathHash());
	const auto hash = h.Get();

	auto* ret = State.Programs.Find(hash);
	if (!ret) { ret = State.Programs.EmplaceYield(hash, *vertexTemplate, *fragmentTemplate); }

	return ret;
}

ShaderTemplate* ShaderManager::GetTemplate(const Path& path, Vulkan::ShaderStage stage) {
	Hasher h(path);
	const auto hash = h.Get();

	auto* ret = State.Shaders.Find(hash);
	if (!ret) {
		auto* shader = State.Shaders.Allocate(path, stage, State.MetaCache, hash, State.IncludeDirs);

		{
			std::lock_guard lock(State.DependencyLock);
			RegisterDependencyNoLock(shader, path);
			shader->RegisterDependencies();
		}

		ret = State.Shaders.InsertYield(hash, shader);
	}

	return ret;
}

void ShaderManager::Recompile(const FileNotifyInfo& info) {
	if (info.Type == FileNotifyType::FileDeleted) { return; }

	std::lock_guard lock(State.DependencyLock);
	for (auto& dep : State.Dependees[info.Path]) {
		Log::Debug("ShaderManager", "Recompiling shader '{}'...", dep->GetPath().String());
		dep->Recompile();
		dep->RegisterDependencies();
	}
}
}  // namespace Luna
