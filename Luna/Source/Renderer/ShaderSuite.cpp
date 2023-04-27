#include <Luna/Renderer/Renderer.hpp>
#include <Luna/Renderer/ShaderSuite.hpp>
#include <Luna/Utility/Log.hpp>
#include <Luna/Vulkan/Device.hpp>

namespace Luna {
void ShaderSuiteResolver::Resolve(ShaderSuite& suite, RendererType type, RenderableType drawable) const {
	if (type == RendererType::GeneralForward || type == RendererType::GeneralDeferred) {
		switch (drawable) {
			case RenderableType::Mesh:
				suite.InitGraphics("res://Shaders/StaticMesh.vert.glsl", "res://Shaders/StaticMesh.frag.glsl");
				break;

			default:
				break;
		}
	} else if (type == RendererType::DepthOnly) {
		switch (drawable) {
			case RenderableType::Mesh:
				suite.InitGraphics("res://Shaders/StaticMesh.vert.glsl", "res://Shaders/StaticMeshDepth.frag.glsl");
				break;

			default:
				break;
		}
	}
}

void ShaderSuite::BakeBaseDefines() {
	Hasher h;
	h(_baseDefines.size());
	for (auto& define : _baseDefines) {
		h(define.first);
		h(define.second);
	}
	_baseDefinesHash = h.Get();
}

Vulkan::Program* ShaderSuite::GetProgram(VariantSignatureKey signature) {
	if (!_program) {
		Log::Error("ShaderSuite", "Missing shader program.");
		return nullptr;
	}

	Hasher h;
	h(_baseDefinesHash);
	const auto hash = h.Get();

	auto* variant = _variants.Find(hash);
	if (!variant) {
		std::vector<std::pair<std::string, int>> defines = _baseDefines;

		auto* programVariant = _program->RegisterVariant(defines);
		variant              = _variants.EmplaceYield(hash, programVariant->GetProgram(), programVariant);

		return variant->CachedProgram;
	} else {
		return variant->IndirectVariant->GetProgram();
	}
}

void ShaderSuite::InitCompute(const Path& computePath) {
	_program = ShaderManager::RegisterCompute(computePath);
	_baseDefines.clear();
	_variants.Clear();
}

void ShaderSuite::InitGraphics(const Path& vertexPath, const Path& fragmentPath) {
	_program = ShaderManager::RegisterGraphics(vertexPath, fragmentPath);
	_baseDefines.clear();
	_variants.Clear();
}

ShaderSuite::Variant::Variant(Vulkan::Program* cachedProgram, ShaderProgramVariant* indirectVariant)
		: CachedProgram(cachedProgram), IndirectVariant(indirectVariant) {}
}  // namespace Luna
