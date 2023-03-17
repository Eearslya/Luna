#include <glslang/SPIRV/GlslangToSpv.h>

#include <Luna/Utility/Log.hpp>
#include <Luna/Vulkan/ShaderCompiler.hpp>

namespace Luna {
namespace Vulkan {
ShaderCompiler::ShaderCompiler() {
	glslang::InitializeProcess();
}

ShaderCompiler::~ShaderCompiler() noexcept {
	glslang::FinalizeProcess();
}

std::optional<std::vector<uint32_t>> ShaderCompiler::Compile(vk::ShaderStageFlagBits stage,
                                                             const std::string& glsl) const {
	EShLanguage lang = EShLangVertex;
	switch (stage) {
		case vk::ShaderStageFlagBits::eVertex:
			lang = EShLangVertex;
			break;
		case vk::ShaderStageFlagBits::eTessellationControl:
			lang = EShLangTessControl;
			break;
		case vk::ShaderStageFlagBits::eTessellationEvaluation:
			lang = EShLangTessEvaluation;
			break;
		case vk::ShaderStageFlagBits::eGeometry:
			lang = EShLangGeometry;
			break;
		case vk::ShaderStageFlagBits::eFragment:
			lang = EShLangFragment;
			break;
		case vk::ShaderStageFlagBits::eCompute:
			lang = EShLangCompute;
			break;
		case vk::ShaderStageFlagBits::eRaygenKHR:
			lang = EShLangRayGen;
			break;
		case vk::ShaderStageFlagBits::eMissKHR:
			lang = EShLangMiss;
			break;
		case vk::ShaderStageFlagBits::eClosestHitKHR:
			lang = EShLangClosestHit;
			break;
		default:
			lang = EShLangVertex;
			break;
	}

	glslang::TShader shader(lang);
	shader.setEnvTarget(glslang::EShTargetLanguage::EShTargetSpv, glslang::EShTargetLanguageVersion::EShTargetSpv_1_4);
	const char* shaderStrings[1] = {glsl.c_str()};
	shader.setStrings(shaderStrings, 1);

	const TBuiltInResource resources{.maxLights                                 = 32,
	                                 .maxClipPlanes                             = 6,
	                                 .maxTextureUnits                           = 32,
	                                 .maxTextureCoords                          = 32,
	                                 .maxVertexAttribs                          = 64,
	                                 .maxVertexUniformComponents                = 4096,
	                                 .maxVaryingFloats                          = 64,
	                                 .maxVertexTextureImageUnits                = 32,
	                                 .maxCombinedTextureImageUnits              = 80,
	                                 .maxTextureImageUnits                      = 32,
	                                 .maxFragmentUniformComponents              = 4096,
	                                 .maxDrawBuffers                            = 32,
	                                 .maxVertexUniformVectors                   = 128,
	                                 .maxVaryingVectors                         = 8,
	                                 .maxFragmentUniformVectors                 = 16,
	                                 .maxVertexOutputVectors                    = 16,
	                                 .maxFragmentInputVectors                   = 15,
	                                 .minProgramTexelOffset                     = -8,
	                                 .maxProgramTexelOffset                     = 7,
	                                 .maxClipDistances                          = 8,
	                                 .maxComputeWorkGroupCountX                 = 65535,
	                                 .maxComputeWorkGroupCountY                 = 65535,
	                                 .maxComputeWorkGroupCountZ                 = 65535,
	                                 .maxComputeWorkGroupSizeX                  = 1024,
	                                 .maxComputeWorkGroupSizeY                  = 1024,
	                                 .maxComputeWorkGroupSizeZ                  = 64,
	                                 .maxComputeUniformComponents               = 1024,
	                                 .maxComputeTextureImageUnits               = 16,
	                                 .maxComputeImageUniforms                   = 8,
	                                 .maxComputeAtomicCounters                  = 8,
	                                 .maxComputeAtomicCounterBuffers            = 1,
	                                 .maxVaryingComponents                      = 60,
	                                 .maxVertexOutputComponents                 = 64,
	                                 .maxGeometryInputComponents                = 64,
	                                 .maxGeometryOutputComponents               = 128,
	                                 .maxFragmentInputComponents                = 128,
	                                 .maxImageUnits                             = 8,
	                                 .maxCombinedImageUnitsAndFragmentOutputs   = 8,
	                                 .maxCombinedShaderOutputResources          = 8,
	                                 .maxImageSamples                           = 0,
	                                 .maxVertexImageUniforms                    = 0,
	                                 .maxTessControlImageUniforms               = 0,
	                                 .maxTessEvaluationImageUniforms            = 0,
	                                 .maxGeometryImageUniforms                  = 0,
	                                 .maxFragmentImageUniforms                  = 8,
	                                 .maxCombinedImageUniforms                  = 8,
	                                 .maxGeometryTextureImageUnits              = 16,
	                                 .maxGeometryOutputVertices                 = 256,
	                                 .maxGeometryTotalOutputComponents          = 1024,
	                                 .maxGeometryUniformComponents              = 1024,
	                                 .maxGeometryVaryingComponents              = 64,
	                                 .maxTessControlInputComponents             = 128,
	                                 .maxTessControlOutputComponents            = 128,
	                                 .maxTessControlTextureImageUnits           = 16,
	                                 .maxTessControlUniformComponents           = 1024,
	                                 .maxTessControlTotalOutputComponents       = 4096,
	                                 .maxTessEvaluationInputComponents          = 128,
	                                 .maxTessEvaluationOutputComponents         = 128,
	                                 .maxTessEvaluationTextureImageUnits        = 16,
	                                 .maxTessEvaluationUniformComponents        = 1024,
	                                 .maxTessPatchComponents                    = 120,
	                                 .maxPatchVertices                          = 32,
	                                 .maxTessGenLevel                           = 64,
	                                 .maxViewports                              = 16,
	                                 .maxVertexAtomicCounters                   = 0,
	                                 .maxTessControlAtomicCounters              = 0,
	                                 .maxTessEvaluationAtomicCounters           = 0,
	                                 .maxGeometryAtomicCounters                 = 0,
	                                 .maxFragmentAtomicCounters                 = 8,
	                                 .maxCombinedAtomicCounters                 = 8,
	                                 .maxAtomicCounterBindings                  = 1,
	                                 .maxVertexAtomicCounterBuffers             = 0,
	                                 .maxTessControlAtomicCounterBuffers        = 0,
	                                 .maxTessEvaluationAtomicCounterBuffers     = 0,
	                                 .maxGeometryAtomicCounterBuffers           = 0,
	                                 .maxFragmentAtomicCounterBuffers           = 1,
	                                 .maxCombinedAtomicCounterBuffers           = 1,
	                                 .maxAtomicCounterBufferSize                = 16384,
	                                 .maxTransformFeedbackBuffers               = 4,
	                                 .maxTransformFeedbackInterleavedComponents = 64,
	                                 .maxCullDistances                          = 8,
	                                 .maxCombinedClipAndCullDistances           = 8,
	                                 .maxSamples                                = 4,
	                                 .maxMeshOutputVerticesNV                   = 256,
	                                 .maxMeshOutputPrimitivesNV                 = 512,
	                                 .maxMeshWorkGroupSizeX_NV                  = 32,
	                                 .maxMeshWorkGroupSizeY_NV                  = 1,
	                                 .maxMeshWorkGroupSizeZ_NV                  = 1,
	                                 .maxTaskWorkGroupSizeX_NV                  = 32,
	                                 .maxTaskWorkGroupSizeY_NV                  = 1,
	                                 .maxTaskWorkGroupSizeZ_NV                  = 1,
	                                 .maxMeshViewCountNV                        = 4,
	                                 .limits                                    = {.nonInductiveForLoops                 = 1,
	                                                                               .whileLoops                           = 1,
	                                                                               .doWhileLoops                         = 1,
	                                                                               .generalUniformIndexing               = 1,
	                                                                               .generalAttributeMatrixVectorIndexing = 1,
	                                                                               .generalVaryingIndexing               = 1,
	                                                                               .generalSamplerIndexing               = 1,
	                                                                               .generalVariableIndexing              = 1,
	                                                                               .generalConstantMatrixVectorIndexing  = 1}};
	const EShMessages messages = static_cast<EShMessages>(EShMsgSpvRules | EShMsgVulkanRules);

	if (!shader.parse(&resources, 100, false, messages)) {
		Log::Error("Vulkan::ShaderCompiler", "Failed to compile {} shader!", vk::to_string(stage));
		Log::Error("Vulkan::ShaderCompiler", "Info Log: {}", shader.getInfoLog());
		Log::Error("Vulkan::ShaderCompiler", "Info Debug Log: {}", shader.getInfoDebugLog());
		return std::nullopt;
	}

	glslang::TProgram program;
	program.addShader(&shader);

	if (!program.link(messages)) {
		Log::Error("Vulkan::ShaderCompiler", "Failed to compile {} shader!", vk::to_string(stage));
		Log::Error("Vulkan::ShaderCompiler", "Info Log: {}", program.getInfoLog());
		Log::Error("Vulkan::ShaderCompiler", "Info Debug Log: {}", program.getInfoDebugLog());
		return std::nullopt;
	}

	std::vector<uint32_t> spirv;
	glslang::GlslangToSpv(*program.getIntermediate(lang), spirv);

	return spirv;
}
}  // namespace Vulkan
}  // namespace Luna
