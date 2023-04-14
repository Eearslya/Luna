#pragma once

#include <Luna/Renderer/Common.hpp>
#include <Luna/Utility/Path.hpp>
#include <Luna/Vulkan/ShaderManager.hpp>

namespace Luna {
class ShaderSuiteResolver {
 public:
	virtual ~ShaderSuiteResolver() noexcept = default;

	virtual void Resolve(Vulkan::Device& device, ShaderSuite& suite, RendererType type, RenderableType drawable) const;
};

struct VariantSignatureKey {};

class ShaderSuite {
 public:
	std::vector<std::pair<std::string, int>>& GetBaseDefines() {
		return _baseDefines;
	}

	void BakeBaseDefines();
	Vulkan::Program* GetProgram(VariantSignatureKey signature);
	void InitCompute(Vulkan::ShaderManager& manager, const Path& computePath);
	void InitGraphics(Vulkan::ShaderManager& manager, const Path& vertexPath, const Path& fragmentPath);

 private:
	struct Variant : IntrusiveHashMapEnabled<Variant> {
		Variant(Vulkan::Program* cachedProgram, Vulkan::ShaderProgramVariant* indirectVariant);

		Vulkan::Program* CachedProgram                = nullptr;
		Vulkan::ShaderProgramVariant* IndirectVariant = nullptr;
	};

	Vulkan::ShaderManager* _manager;
	Vulkan::ShaderProgram* _program = nullptr;
	std::vector<std::pair<std::string, int>> _baseDefines;
	Hash _baseDefinesHash = 0;
	ThreadSafeIntrusiveHashMapReadCached<Variant> _variants;
};
}  // namespace Luna
