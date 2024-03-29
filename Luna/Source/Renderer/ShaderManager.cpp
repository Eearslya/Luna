#include <Luna/Core/Filesystem.hpp>
#include <Luna/Core/Threading.hpp>
#include <Luna/Renderer/Renderer.hpp>
#include <Luna/Renderer/ShaderCompiler.hpp>
#include <Luna/Renderer/ShaderManager.hpp>
#include <Luna/Vulkan/Device.hpp>
#include <Luna/Vulkan/Shader.hpp>

namespace Luna {
struct Notify {
	FilesystemBackend* Backend = nullptr;
	FileNotifyHandle Handle    = {};
};

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

static struct ShaderManagerState {
	MetaCache MetaCache;
	Vulkan::VulkanCache<ShaderTemplate> Shaders;
	Vulkan::VulkanCache<ShaderProgram> Programs;
	std::vector<Path> IncludeDirs;
	std::unordered_map<Path, std::unordered_set<ShaderTemplate*>> Dependees;
	std::mutex DependencyLock;
	std::unordered_map<Path, Notify> DirectoryWatches;
} State;

static ShaderTemplate* GetTemplate(const Path& path, Vulkan::ShaderStage stage);
static void PromoteReadWriteCachesToReadOnly();
static void Recompile(const FileNotifyInfo& info);
static void RegisterDependency(ShaderTemplate* shader, const Path& dependency);
static void RegisterDependencyNoLock(ShaderTemplate* shader, const Path& dependency);

static ShaderTemplate* GetTemplate(const Path& path, Vulkan::ShaderStage stage) {
	Hasher h(path);
	const auto hash = h.Get();

	auto* ret = State.Shaders.Find(hash);
	if (!ret) {
		auto* shader = State.Shaders.Allocate(path, stage, hash, State.IncludeDirs);

		{
			std::lock_guard lock(State.DependencyLock);
			RegisterDependencyNoLock(shader, path);
			shader->RegisterDependencies();
		}

		ret = State.Shaders.InsertYield(hash, shader);
	}

	return ret;
}

static void PromoteReadWriteCachesToReadOnly() {
	State.MetaCache.ShaderToLayout.MoveToReadOnly();
	State.MetaCache.VariantToShader.MoveToReadOnly();
	State.Programs.MoveToReadOnly();
	State.Shaders.MoveToReadOnly();
}

static void Recompile(const FileNotifyInfo& info) {
	if (info.Type == FileNotifyType::FileDeleted) { return; }

	// A lot of the time when we get a notification, the file is still locked for writing.
	// Therefore, we introduce a small delay here to allow the file to unlock before trying
	// to read it.
	Threading::Sleep(100);

	std::lock_guard lock(State.DependencyLock);
	for (auto& dep : State.Dependees[info.Path]) {
		Log::Debug("ShaderManager", "Recompiling shader '{}'...", dep->GetPath());
		dep->Recompile();
		dep->RegisterDependencies();
	}
}

static void RegisterDependency(ShaderTemplate* shader, const Path& dependency) {
	std::lock_guard lock(State.DependencyLock);
	RegisterDependencyNoLock(shader, dependency);
}

static void RegisterDependencyNoLock(ShaderTemplate* shader, const Path& dependency) {
	State.Dependees[dependency].insert(shader);

	const Path baseDir = dependency.ParentPath();
	if (State.DirectoryWatches.find(baseDir) != State.DirectoryWatches.end()) { return; }

	auto* backend = Filesystem::GetBackend(baseDir.Protocol());
	if (!backend) { return; }

	FileNotifyHandle handle = backend->WatchFile(baseDir.FilePath(), [](const FileNotifyInfo& info) {
		Threading::CreateTaskGroup()->Enqueue([=]() { Recompile(info); });
	});
	if (handle >= 0) { State.DirectoryWatches[baseDir] = {backend, handle}; }
}

Vulkan::Shader* ShaderTemplateVariant::Resolve() const {
	if (PrecompiledShader) {
		return PrecompiledShader;
	} else if (Spirv.empty() && SpirvHash == 0) {
		return nullptr;
	} else if (Spirv.empty()) {
		return Renderer::GetDevice().RequestShader(SpirvHash);
	} else {
		return Renderer::GetDevice().RequestShader(Spirv.size() * sizeof(uint32_t), Spirv.data());
	}
}

ShaderTemplate::ShaderTemplate(const Path& shaderPath,
                               Vulkan::ShaderStage stage,
                               Hash pathHash,
                               const std::vector<Path>& includeDirs)
		: _path(shaderPath), _pathHash(pathHash), _stage(stage), _includeDirs(includeDirs) {
	const auto ext = _path.Extension();
	if (ext == "spv") {
		auto spvFile = Filesystem::OpenReadOnlyMapping(_path);
		if (!spvFile) { throw std::runtime_error("[ShaderTemplate] Failed to load Shader SPV file."); }

		_staticShader = {spvFile->Data<uint32_t>(), spvFile->Data<uint32_t>() + (spvFile->GetSize() / sizeof(uint32_t))};
		_sourceHash   = 0;
	} else {
		_compiler = std::make_unique<ShaderCompiler>();
		_compiler->SetSourceFromFile(_path, _stage);
		_compiler->SetIncludeDirectories(_includeDirs);
		if (!_compiler->Preprocess()) {
			_compiler.reset();

			Log::Error("ShaderManager", "Failed to preprocess {} shader!", VulkanEnumToString(_stage));
			throw std::runtime_error("[ShaderManager] Failed to preprocess shader source.");
		}
		_sourceHash = _compiler->GetSourceHash();
	}
}

ShaderTemplate::~ShaderTemplate() noexcept {}

void ShaderTemplate::Recompile() {
	Log::Debug("ShaderManager", "Recompiling {} shader: {}", VulkanEnumToString(_stage), _path);
	auto newCompiler = std::make_unique<ShaderCompiler>();
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
		for (auto& dep : _compiler->GetDependencies()) { RegisterDependencyNoLock(this, dep); }
	}
}

const ShaderTemplateVariant* ShaderTemplate::RegisterVariant(const std::vector<std::pair<std::string, int>>& defines,
                                                             Vulkan::Shader* precompiledShader) {
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

		PrecomputedMeta* precompiledSpirv = nullptr;
		if (!precompiledShader) {
			precompiledSpirv = State.MetaCache.VariantToShader.Find(completeHash);

			if (precompiledSpirv) {
				if (!Renderer::GetDevice().RequestShader(precompiledSpirv->ShaderHash)) {
					precompiledSpirv = nullptr;
				} else if (_sourceHash != precompiledSpirv->SourceHash) {
					precompiledSpirv = nullptr;
				}
			}
		}

		if (precompiledShader) {
			variant->PrecompiledShader = precompiledShader;
		} else if (!precompiledSpirv) {
			if (!_staticShader.empty()) {
				variant->Spirv = _staticShader;
				UpdateVariantCache(*variant);
			} else if (_compiler) {
				std::string error;
				variant->Spirv = _compiler->Compile(error, defines);

				if (variant->Spirv.empty()) {
					Log::Error("ShaderManager", "Failed to compile {} shader:\n{}", VulkanEnumToString(_stage), error);
					variant->Spirv.clear();
					variant->SpirvHash = 0;
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
	Log::Debug("ShaderManager", "Recompiling shader variant...");
	std::string error;
	auto newSpv = _compiler->Compile(error, variant.Defines);
	if (newSpv.empty()) {
		Log::Error("ShaderManager", "Failed to recompile {} shader:\n{}", VulkanEnumToString(_stage), error);
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

	const auto layout = Vulkan::Shader::Reflect(variant.Spirv.size() * sizeof(uint32_t), variant.Spirv.data());
	auto* varToShader = State.MetaCache.VariantToShader.Find(variant.VariantHash);
	if (varToShader) {
		varToShader->SourceHash = _sourceHash;
		varToShader->ShaderHash = shaderHash;
	} else {
		State.MetaCache.VariantToShader.EmplaceYield(variant.VariantHash, _sourceHash, shaderHash);
	}

	State.MetaCache.ShaderToLayout.EmplaceYield(shaderHash, layout);
}

ShaderProgramVariant::ShaderProgramVariant() {
	for (auto& inst : _shaderInstance) { inst.store(0, std::memory_order_relaxed); }
	_program.store(nullptr, std::memory_order_relaxed);
	_stages.fill(nullptr);
}

ShaderProgramVariant::~ShaderProgramVariant() noexcept {}

Vulkan::Program* ShaderProgramVariant::GetProgram() {
	auto* frag = _stages[int(Vulkan::ShaderStage::Fragment)];
	auto* comp = _stages[int(Vulkan::ShaderStage::Compute)];

	if (comp) {
		return GetCompute();
	} else if (frag) {
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
	_stages.fill(nullptr);
	SetStage(Vulkan::ShaderStage::Compute, &compute);
}

ShaderProgram::ShaderProgram(ShaderTemplate& vertex, ShaderTemplate& fragment) {
	_stages.fill(nullptr);
	SetStage(Vulkan::ShaderStage::Vertex, &vertex);
	SetStage(Vulkan::ShaderStage::Fragment, &fragment);
}

ShaderProgram::~ShaderProgram() noexcept {}

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

Vulkan::Program* ShaderManager::GetGraphics(const Path& vertex,
                                            const Path& fragment,
                                            const std::vector<std::pair<std::string, int>>& defines) {
	auto* program = RegisterGraphics(vertex, fragment);
	if (!program) { return nullptr; }

	auto* variant = program->RegisterVariant(defines);
	if (!variant) { return nullptr; }

	return variant->GetProgram();
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
}  // namespace Luna
