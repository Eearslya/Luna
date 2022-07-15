#pragma once

#include <optional>

#include "Common.hpp"

namespace Luna {
namespace Vulkan {
class ShaderCompiler {
 public:
	ShaderCompiler();
	~ShaderCompiler() noexcept;

	std::optional<std::vector<uint32_t>> Compile(vk::ShaderStageFlagBits stage, const std::string& glsl) const;
};
}  // namespace Vulkan
}  // namespace Luna
