#include <Luna/Platform/Filesystem.hpp>
#include <Luna/Renderer/GlslCompiler.hpp>
#include <Luna/Utility/Log.hpp>
#include <Luna/Utility/String.hpp>
#include <shaderc/shaderc.hpp>

namespace Luna {
std::vector<uint32_t> GlslCompiler::Compile(std::string& error,
                                            const std::vector<std::pair<std::string, int>>& defines) {
	shaderc::Compiler compiler;
	shaderc::CompileOptions options;
	if (_processedSource.empty()) {
		error = "Must call Preprocess() before compiling!";
		return {};
	}

	for (const auto& def : defines) { options.AddMacroDefinition(def.first, std::to_string(def.second)); }
	options.SetOptimizationLevel(shaderc_optimization_level_performance);
	options.SetGenerateDebugInfo();
	options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_2);
	options.SetSourceLanguage(shaderc_source_language_glsl);

	shaderc_shader_kind shaderKind;
	switch (_stage) {
		case Vulkan::ShaderStage::Vertex:
			shaderKind = shaderc_glsl_vertex_shader;
			break;
		case Vulkan::ShaderStage::TessellationControl:
			shaderKind = shaderc_glsl_tess_control_shader;
			break;
		case Vulkan::ShaderStage::TessellationEvaluation:
			shaderKind = shaderc_glsl_tess_evaluation_shader;
			break;
		case Vulkan::ShaderStage::Geometry:
			shaderKind = shaderc_glsl_geometry_shader;
			break;
		case Vulkan::ShaderStage::Fragment:
			shaderKind = shaderc_glsl_fragment_shader;
			break;
		case Vulkan::ShaderStage::Compute:
			shaderKind = shaderc_glsl_compute_shader;
			break;
		default:
			error = "Invalid shader stage.";
			return {};
	}

	error.clear();
	const auto result =
		compiler.CompileGlslToSpv(_processedSource.c_str(), shaderKind, _sourcePath.String().c_str(), options);
	if (result.GetCompilationStatus() != shaderc_compilation_status_success) {
		error = result.GetErrorMessage();
		return {};
	}

	std::vector<uint32_t> spv(result.cbegin(), result.cend());

	return spv;
}

bool GlslCompiler::Preprocess() {
	_dependencies.clear();
	_processedSource.clear();

	if (_source.empty()) { return false; }

	try {
		if (!Parse(_sourcePath, _source)) { return false; }
	} catch (const std::exception& e) {
		Log::Error("GlslCompiler",
		           "Failed to preprocess {} shader '{}': {}",
		           Vulkan::VulkanEnumToString(_stage),
		           _sourcePath.String(),
		           e.what());

		return false;
	}
	_sourceHash = Hasher(_processedSource).Get();

	return true;
}

void GlslCompiler::SetIncludeDirectories(const std::vector<Path>& includeDirs) {
	_includeDirs = includeDirs;
}

void GlslCompiler::SetSourceFromFile(const Path& path, Vulkan::ShaderStage stage) {
	auto* filesystem = Filesystem::Get();
	if (!filesystem->ReadFileToString(path, _source)) { return; }
	_sourcePath = path;
	_sourceHash = Hasher(_source).Get();
	_stage      = stage;
}

bool GlslCompiler::Parse(const Path& sourcePath, const std::string& source) {
	auto lines         = StringSplit(source, "\n");
	uint32_t lineIndex = 1;
	size_t offset      = 0;

	for (auto& line : lines) {
		if ((offset = line.find("//")) != std::string::npos) { line = line.substr(0, offset); }

		if ((offset = line.find("#include \"")) != std::string::npos) {
			auto includePath = line.substr(offset + 10);
			if (!includePath.empty() && includePath.back() == '"') { includePath.pop_back(); }

			const auto [includedPath, includedSource] = ResolveInclude(sourcePath, includePath);
			_processedSource += fmt::format("#line 1 \"{}\"\n", includedPath.String());
			if (!Parse(includedPath, includedSource)) { return false; }
			_processedSource += fmt::format("#line {} \"{}\"\n", lineIndex + 1, sourcePath.String());

			_dependencies.push_back(includedPath);
		} else {
			_processedSource += line;
			_processedSource += '\n';

			const auto firstNonSpace = line.find_first_not_of(' ');
			if (firstNonSpace != std::string::npos && line[firstNonSpace] == '#') {}
		}

		++lineIndex;
	}

	return true;
}

std::pair<Path, std::string> GlslCompiler::ResolveInclude(const Path& sourcePath, const std::string& includePath) {
	auto* filesystem = Filesystem::Get();

	auto includedPath = sourcePath.Relative(includePath);
	std::string includedSource;
	if (filesystem->ReadFileToString(includedPath, includedSource)) { return {includedPath, includedSource}; }

	for (const auto& dir : _includeDirs) {
		includedPath = dir / includePath;
		if (filesystem->ReadFileToString(includedPath, includedSource)) { return {includedPath, includedSource}; }
	}

	throw std::runtime_error("[GlslCompiler] Could not resolve include!");
}
}  // namespace Luna
