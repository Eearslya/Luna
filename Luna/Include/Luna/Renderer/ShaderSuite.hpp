#pragma once

#include <Luna/Renderer/Common.hpp>
#include <Luna/Renderer/ShaderManager.hpp>
#include <Luna/Utility/Path.hpp>

namespace Luna {
class ShaderSuiteResolver {
 public:
	virtual ~ShaderSuiteResolver() noexcept = default;

	virtual void Resolve(ShaderSuite& suite, RendererType type, RenderableType drawable) const;
};

struct VariantSignatureKey {};

class ShaderSuite {
 public:
	std::vector<std::pair<std::string, int>>& GetBaseDefines() {
		return _baseDefines;
	}

	void BakeBaseDefines();
	Vulkan::Program* GetProgram(VariantSignatureKey signature);
	void InitCompute(const Path& computePath);
	void InitGraphics(const Path& vertexPath, const Path& fragmentPath);

 private:
	struct Variant : IntrusiveHashMapEnabled<Variant> {
		Variant(Vulkan::Program* cachedProgram, ShaderProgramVariant* indirectVariant);

		Vulkan::Program* CachedProgram        = nullptr;
		ShaderProgramVariant* IndirectVariant = nullptr;
	};

	ShaderProgram* _program = nullptr;
	std::vector<std::pair<std::string, int>> _baseDefines;
	Hash _baseDefinesHash = 0;
	ThreadSafeIntrusiveHashMapReadCached<Variant> _variants;
};
}  // namespace Luna
