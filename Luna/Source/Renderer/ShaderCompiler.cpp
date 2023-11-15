#include <Luna/Core/Filesystem.hpp>
#include <Luna/Renderer/ShaderCompiler.hpp>
#include <Luna/Utility/String.hpp>
#include <shaderc/shaderc.hpp>

namespace Luna {
std::vector<uint32_t> ShaderCompiler::Compile(std::string& error,
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

	return std::vector<uint32_t>(result.cbegin(), result.cend());
}

bool ShaderCompiler::Preprocess() {
	_dependencies.clear();
	_processedSource.clear();

	if (_source.empty()) { return false; }

	try {
		if (!Parse(_sourcePath, _source)) { return false; }
	} catch (const std::exception& e) {
		Log::Error("ShaderCompiler", "Failed to preprocess {} shader '{}': {}", _stage, _sourcePath, e.what());

		return false;
	}
	_sourceHash = Hasher(_processedSource).Get();

	return true;
}

void ShaderCompiler::SetIncludeDirectories(const std::vector<Path>& includeDirs) {
	_includeDirs = includeDirs;
}

void ShaderCompiler::SetSourceFromFile(const Path& path, Vulkan::ShaderStage stage) {
	if (!Filesystem::ReadFileToString(path, _source)) { return; }
	_sourcePath = path;
	_sourceHash = Hasher(_source).Get();
	_stage      = stage;
}

bool ShaderCompiler::Parse(const Path& sourcePath, const std::string& source) {
	auto lines         = StringSplit(source, "\n");
	uint32_t lineIndex = 1;
	size_t offset      = 0;

	for (auto& line : lines) {
		// Strip comments from the code. (TODO: Handle block comments.)
		if ((offset = line.find("//")) != std::string::npos) { line = line.substr(0, offset); }

		// Handle include directives.
		if ((offset = line.find("#include \"")) != std::string::npos) {
			// Find the path of our included file.
			auto includePath = line.substr(offset + 10);
			if (!includePath.empty() && includePath.back() == '"') { includePath.pop_back(); }

			// Load the actual path and source code for the included file.
			const auto [includedPath, includedSource] = ResolveInclude(sourcePath, includePath);

			// Prevent including the same file twice.
			const auto it = std::find(_dependencies.begin(), _dependencies.end(), includedPath);
			if (it == _dependencies.end()) {
				// Use a #line directive to tell the compiler we're starting at line 1 of this included file.
				_processedSource += fmt::format("#line 1 \"{}\"\n", includedPath.String());

				// Append the included file's source.
				if (!Parse(includedPath, includedSource)) { return false; }

				// Use another #line directive to tell the compiler to go back to where we were in this file.
				_processedSource += fmt::format("#line {} \"{}\"\n", lineIndex + 1, sourcePath.String());

				// Add the included file to our list of dependencies.
				_dependencies.push_back(includedPath);
			}
		} else {
			_processedSource += line;
			_processedSource += '\n';
		}

		++lineIndex;
	}

	return true;
}

std::pair<Path, std::string> ShaderCompiler::ResolveInclude(const Path& sourcePath, const std::string& includePath) {
	// First try and load the include path as if it were relative to the source file.
	auto includedPath = sourcePath.Relative(includePath);
	std::string includedSource;
	if (Filesystem::ReadFileToString(includedPath, includedSource)) { return {includedPath, includedSource}; }

	// If that doesn't work, try loading it relative to each include directory.
	for (const auto& dir : _includeDirs) {
		includedPath = dir / includePath;
		if (Filesystem::ReadFileToString(includedPath, includedSource)) { return {includedPath, includedSource}; }
	}

	Log::Error("ShaderCompiler", "Failed to resolve included file '{}', included from '{}'.", includePath, sourcePath);

	throw std::runtime_error("Failed to resolve GLSL include!");
}
}  // namespace Luna
