#include "Model.hpp"

#include <mikktspace.h>
#include <stb_image.h>

#include <Luna/Utility/Hash.hpp>
#include <Luna/Vulkan/Buffer.hpp>
#include <Luna/Vulkan/Device.hpp>
#include <Luna/Vulkan/Image.hpp>
#include <Luna/Vulkan/Sampler.hpp>
#include <fastgltf_parser.hpp>
#include <fastgltf_types.hpp>
#include <fstream>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/normal.hpp>
#include <iostream>
#include <optional>

#include "Files.hpp"

static constexpr bool ApplyTransforms = true;
static constexpr bool MergeSubmeshes  = true;

namespace fastgltf {
std::string to_string(AccessorType type) {
	switch (type) {
		case AccessorType::Scalar:
			return "Scalar";
		case AccessorType::Vec2:
			return "Vec2";
		case AccessorType::Vec3:
			return "Vec3";
		case AccessorType::Vec4:
			return "Vec4";
		case AccessorType::Mat2:
			return "Mat2";
		case AccessorType::Mat3:
			return "Mat3";
		case AccessorType::Mat4:
			return "Mat4";
		default:
			return "Invalid";
	}
}

std::string to_string(ComponentType type) {
	switch (type) {
		case ComponentType::Byte:
			return "Byte";
		case ComponentType::UnsignedByte:
			return "UnsignedByte";
		case ComponentType::Short:
			return "Short";
		case ComponentType::UnsignedShort:
			return "UnsignedShort";
		case ComponentType::UnsignedInt:
			return "UnsignedInt";
		case ComponentType::Float:
			return "Float";
		case ComponentType::Double:
			return "Double";
		default:
			return "Invalid";
	}
}
}  // namespace fastgltf

template <typename T>
struct AccessorType {
	using UnderlyingT                                  = void;
	constexpr static fastgltf::AccessorType Type       = fastgltf::AccessorType::Invalid;
	constexpr static fastgltf::ComponentType Component = fastgltf::ComponentType::Invalid;
	constexpr static size_t Count                      = 0;
};
template <>
struct AccessorType<glm::vec2> {
	using UnderlyingT                                  = typename glm::vec2::value_type;
	constexpr static fastgltf::AccessorType Type       = fastgltf::AccessorType::Vec2;
	constexpr static fastgltf::ComponentType Component = fastgltf::ComponentType::Float;
	constexpr static size_t Count                      = 2;
};
template <>
struct AccessorType<glm::vec3> {
	using UnderlyingT                                  = typename glm::vec3::value_type;
	constexpr static fastgltf::AccessorType Type       = fastgltf::AccessorType::Vec3;
	constexpr static fastgltf::ComponentType Component = fastgltf::ComponentType::Float;
	constexpr static size_t Count                      = 3;
};
template <>
struct AccessorType<glm::vec4> {
	using UnderlyingT                                  = typename glm::vec4::value_type;
	constexpr static fastgltf::AccessorType Type       = fastgltf::AccessorType::Vec4;
	constexpr static fastgltf::ComponentType Component = fastgltf::ComponentType::Float;
	constexpr static size_t Count                      = 4;
};
template <>
struct AccessorType<glm::uvec4> {
	using UnderlyingT                                  = typename glm::uvec4::value_type;
	constexpr static fastgltf::AccessorType Type       = fastgltf::AccessorType::Vec4;
	constexpr static fastgltf::ComponentType Component = fastgltf::ComponentType::UnsignedInt;
	constexpr static size_t Count                      = 4;
};
template <>
struct AccessorType<uint8_t> {
	using UnderlyingT                                  = uint8_t;
	constexpr static fastgltf::AccessorType Type       = fastgltf::AccessorType::Scalar;
	constexpr static fastgltf::ComponentType Component = fastgltf::ComponentType::UnsignedByte;
	constexpr static size_t Count                      = 1;
};
template <>
struct AccessorType<uint16_t> {
	using UnderlyingT                                  = uint16_t;
	constexpr static fastgltf::AccessorType Type       = fastgltf::AccessorType::Scalar;
	constexpr static fastgltf::ComponentType Component = fastgltf::ComponentType::UnsignedShort;
	constexpr static size_t Count                      = 1;
};
template <>
struct AccessorType<uint32_t> {
	using UnderlyingT                                  = uint32_t;
	constexpr static fastgltf::AccessorType Type       = fastgltf::AccessorType::Scalar;
	constexpr static fastgltf::ComponentType Component = fastgltf::ComponentType::UnsignedInt;
	constexpr static size_t Count                      = 1;
};

template <class... Ts>
struct Overloaded : Ts... {
	using Ts::operator()...;
};
template <class... Ts>
Overloaded(Ts...) -> Overloaded<Ts...>;

enum class VertexAttributeBits {
	Position  = 1 << 1,
	Normal    = 1 << 2,
	Tangent   = 1 << 3,
	Texcoord0 = 1 << 4,
	Texcoord1 = 1 << 5,
	Color0    = 1 << 6,
	Joints0   = 1 << 7,
	Weights0  = 1 << 8,
	Index     = 1 << 9
};
using VertexAttributes = Luna::Bitmask<VertexAttributeBits>;
template <>
struct Luna::EnableBitmaskOperators<VertexAttributeBits> : std::true_type {};

enum class MeshProcessingStepBits {
	UnpackVertices       = 1 << 1,
	GenerateFlatNormals  = 1 << 2,
	GenerateTangentSpace = 1 << 3,
	WeldVertices         = 1 << 4
};
using MeshProcessingSteps = Luna::Bitmask<MeshProcessingStepBits>;
template <>
struct Luna::EnableBitmaskOperators<MeshProcessingStepBits> : std::true_type {};

void Material::Update(Luna::Vulkan::Device& device) const {
	Data.AlbedoTransform    = bool(Albedo) ? AlbedoTransform : glm::mat3(1.0f);
	Data.NormalTransform    = bool(Normal) ? NormalTransform : glm::mat3(1.0f);
	Data.PBRTransform       = bool(PBR) ? PBRTransform : glm::mat3(1.0f);
	Data.OcclusionTransform = bool(Occlusion) ? OcclusionTransform : glm::mat3(1.0f);
	Data.EmissiveTransform  = bool(Emissive) ? EmissiveTransform : glm::mat3(1.0f);

	Data.BaseColorFactor = BaseColorFactor;
	Data.EmissiveFactor  = glm::vec4(EmissiveFactor, 0.0f);

	Data.AlbedoIndex     = bool(Albedo) ? Albedo->BoundIndex : -1;
	Data.NormalIndex     = bool(Normal) ? Normal->BoundIndex : -1;
	Data.PBRIndex        = bool(PBR) ? PBR->BoundIndex : -1;
	Data.OcclusionIndex  = bool(Occlusion) ? Occlusion->BoundIndex : -1;
	Data.EmissiveIndex   = bool(Emissive) ? Emissive->BoundIndex : -1;
	Data.AlbedoUV        = bool(Albedo) ? AlbedoUV : -1;
	Data.NormalUV        = bool(Normal) ? NormalUV : -1;
	Data.PBRUV           = bool(PBR) ? PBRUV : -1;
	Data.OcclusionUV     = bool(Occlusion) ? OcclusionUV : -1;
	Data.EmissiveUV      = bool(Emissive) ? EmissiveUV : -1;
	Data.DoubleSided     = Sidedness == Sidedness::Both ? 1 : 0;
	Data.AlphaMode       = AlphaMode == AlphaMode::Mask ? 1 : 0;
	Data.AlphaCutoff     = AlphaCutoff;
	Data.MetallicFactor  = MetallicFactor;
	Data.RoughnessFactor = RoughnessFactor;
	Data.OcclusionFactor = OcclusionFactor;

	const auto dataHash = Luna::Hasher(Data).Get();
	const bool update   = dataHash != DataHash || !DataBuffer;
	if (update) {
		if (!DataBuffer) {
			DataBuffer = device.CreateBuffer(
				Luna::Vulkan::BufferCreateInfo{
					Luna::Vulkan::BufferDomain::Host, sizeof(Data), vk::BufferUsageFlagBits::eUniformBuffer},
				&Data);
		}

		MaterialData* bufferData = reinterpret_cast<MaterialData*>(DataBuffer->Map());
		memcpy(bufferData, &Data, sizeof(Data));
	}
	DataHash = dataHash;
}

struct MikkTContext {
	std::vector<Vertex>& Vertices;
	Material* Material = nullptr;
};

/* ============================
** === MikkTSpace Functions ===
** ============================ */
static int MikkTGetNumFaces(const SMikkTSpaceContext* context) {
	const auto data = reinterpret_cast<const MikkTContext*>(context->m_pUserData);
	return data->Vertices.size() / 3;
}
static int MikkTGetNumVerticesOfFace(const SMikkTSpaceContext* context, const int face) {
	return 3;
}
static void MikkTGetPosition(const SMikkTSpaceContext* context, float fvPosOut[], const int face, const int vert) {
	const auto data     = reinterpret_cast<const MikkTContext*>(context->m_pUserData);
	const glm::vec3 pos = data->Vertices[face * 3 + vert].Position;
	fvPosOut[0]         = pos.x;
	fvPosOut[1]         = pos.y;
	fvPosOut[2]         = pos.z;
}
static void MikkTGetNormal(const SMikkTSpaceContext* context, float fvNormOut[], const int face, const int vert) {
	const auto data      = reinterpret_cast<const MikkTContext*>(context->m_pUserData);
	const glm::vec3 norm = data->Vertices[face * 3 + vert].Normal;
	fvNormOut[0]         = norm.x;
	fvNormOut[1]         = norm.y;
	fvNormOut[2]         = norm.z;
}
static void MikkTGetTexCoord(const SMikkTSpaceContext* context, float fvTexcOut[], const int face, const int vert) {
	const auto data = reinterpret_cast<const MikkTContext*>(context->m_pUserData);
	glm::vec2 uv    = data->Vertices[face * 3 + vert].Texcoord0;
	uv              = data->Material->NormalTransform * glm::vec3(uv, 1.0f);
	fvTexcOut[0]    = uv.x;
	fvTexcOut[1]    = 1.0f - uv.y;
}
static void MikkTSetTSpaceBasic(
	const SMikkTSpaceContext* context, const float fvTangent[], const float fSign, const int face, const int vert) {
	auto data = reinterpret_cast<MikkTContext*>(context->m_pUserData);

	data->Vertices[face * 3 + vert].Tangent = glm::vec4(glm::make_vec3(fvTangent), fSign);
}
static SMikkTSpaceInterface MikkTInterface = {.m_getNumFaces          = MikkTGetNumFaces,
                                              .m_getNumVerticesOfFace = MikkTGetNumVerticesOfFace,
                                              .m_getPosition          = MikkTGetPosition,
                                              .m_getNormal            = MikkTGetNormal,
                                              .m_getTexCoord          = MikkTGetTexCoord,
                                              .m_setTSpaceBasic       = MikkTSetTSpaceBasic,
                                              .m_setTSpace            = nullptr};

/* ========================
** === Helper Functions ===
   ======================== */
template <typename T>
static vk::DeviceSize GetAlignedSize(vk::DeviceSize count) {
	return ((count * sizeof(T)) + 16llu) & ~16llu;
}
static std::unique_ptr<fastgltf::Asset> ParseGltf(const std::filesystem::path& gltfPath) {
	fastgltf::Parser parser(fastgltf::Extensions::KHR_mesh_quantization | fastgltf::Extensions::KHR_texture_transform);
	fastgltf::GltfDataBuffer gltfData;
	gltfData.loadFromFile(gltfPath);

	const auto gltfFile = gltfPath.string();
	const auto gltfExt  = gltfPath.extension().string();
	std::unique_ptr<fastgltf::glTF> loaded;
	if (gltfExt == ".gltf") {
		loaded = parser.loadGLTF(
			&gltfData, gltfPath.parent_path(), fastgltf::Options::LoadGLBBuffers | fastgltf::Options::LoadExternalBuffers);
	} else if (gltfExt == ".glb") {
		loaded = parser.loadBinaryGLTF(&gltfData, gltfPath.parent_path(), fastgltf::Options::LoadGLBBuffers);
	} else {
		std::cerr << "[GltfImporter] Mesh asset file " << gltfFile << " is not supported!\n";
		return nullptr;
	}

	fastgltf::Error loadError = fastgltf::Error::None;

	loadError = parser.getError();
	if (loadError != fastgltf::Error::None) {
		std::cerr << "[GltfImporter] Failed to load mesh asset file" << gltfFile << ".\n";
		std::cerr << "[GltfImporter] fastgltf error: " << static_cast<int>(loadError) << std::endl;
		return nullptr;
	}
	loadError = loaded->parse(fastgltf::Category::All | fastgltf::Category::Animations);
	if (loadError != fastgltf::Error::None) {
		std::cerr << "[GltfImporter] Failed to parse mesh asset file" << gltfFile << ".\n";
		std::cerr << "[GltfImporter] fastgltf error: " << static_cast<int>(loadError) << std::endl;
		return nullptr;
	}
	loadError = loaded->validate();
	if (loadError != fastgltf::Error::None) {
		std::cerr << "[GltfImporter] Failed to validate mesh asset file" << gltfFile << ".\n";
		std::cerr << "[GltfImporter] fastgltf error: " << static_cast<int>(loadError) << std::endl;
		return nullptr;
	}

	return loaded->getParsedAsset();
}

Model::Model(Luna::Vulkan::Device& device, const std::filesystem::path& gltfPath) {
	ProfileTimer loadTimer;

	ProfileTimer parseTimer;
	auto gltf = ParseGltf(gltfPath);
	if (!gltf) { throw std::runtime_error("Failed to load glTF file!"); }
	auto& gltfModel = *gltf;
	_timeParse      = parseTimer.Get();

	ProfileTimer bufferTimer;
	for (auto& gltfBuffer : gltfModel.buffers) {
		std::visit(Overloaded{
								 [](auto& arg) { throw std::runtime_error("Data buffer was not loaded!"); },
								 [&](fastgltf::sources::Vector& vector) {},
							 },
		           gltfBuffer.data);
	}
	_timeBufferLoad = bufferTimer.Get();

	ImportImages(gltfModel, gltfPath, device);
	ImportSamplers(gltfModel, device);
	ImportTextures(gltfModel);
	ImportMaterials(gltfModel);
	{
		ProfileTimer meshLoad;
		ImportMeshes(gltfModel, device);
		_timeMeshLoad = meshLoad.Get();
	}
	ImportNodes(gltfModel);
	ImportSkins(gltfModel, device);
	ImportAnimations(gltfModel);

	Name                  = gltfPath.filename().string();
	const auto& gltfScene = gltfModel.scenes[gltfModel.defaultScene ? gltfModel.defaultScene.value() : 0];
	if (!gltfScene.name.empty()) { Name = gltfScene.name; }

	for (auto* nodePtr : RootNodes) { CalculateBounds(nodePtr, nullptr); }
	for (auto& node : _nodes) {
		if (node->BVH.Valid) {
			_minDim = glm::min(_minDim, node->BVH.Min);
			_maxDim = glm::max(_maxDim, node->BVH.Max);
		}
	}

	AABB = glm::scale(glm::mat4(1.0f), glm::vec3(_maxDim.x - _minDim.x, _maxDim.y - _minDim.y, _maxDim.z - _minDim.z));
	AABB[3][0] = _minDim.x;
	AABB[3][1] = _minDim.y;
	AABB[3][2] = _minDim.z;

	ResetAnimation();

	const double timeLoad = loadTimer.Get();
	std::cout << "\tLoading completed in " << timeLoad * 1000.0 << "ms.\n";
	std::cout << "\t\tglTF Parse: " << _timeParse * 1000.0 << "ms" << std::endl;
	std::cout << "\t\tBuffer Load: " << _timeBufferLoad * 1000.0 << "ms" << std::endl;
	std::cout << "\t\tMesh Load: " << _timeMeshLoad * 1000.0 << "ms" << std::endl;
	std::cout << "\t\t\tLoad Vertices: " << _timeVertexLoad * 1000.0 << "ms" << std::endl;
	std::cout << "\t\t\tUnpack Vertices: " << _timeUnpackVertices * 1000.0 << "ms" << std::endl;
	std::cout << "\t\t\tGenerate Flat Normals: " << _timeGenerateFlatNormals * 1000.0 << "ms" << std::endl;
	std::cout << "\t\t\tGenerate Tangent Space: " << _timeGenerateTangents * 1000.0 << "ms" << std::endl;
	std::cout << "\t\t\tWeld Vertices: " << _timeWeldVertices * 1000.0 << "ms" << std::endl;
}

void Model::ResetAnimation() {
	for (auto& node : _nodes) { node->ResetAnimation(); }
}

void Model::CalculateBounds(Node* node, Node* parent) {
	if (node->Mesh) {
		if (node->Mesh->Bounds.Valid) {
			node->AABB = node->Mesh->Bounds.Transform(node->GetGlobalTransform());
			if (node->Children.size() == 0) {
				node->BVH       = node->AABB;
				node->BVH.Valid = true;
			}
		}
	}

	for (auto* child : node->Children) { CalculateBounds(child, node); }
}

void Model::ImportAnimations(const fastgltf::Asset& gltfModel) {
	for (size_t i = 0; i < gltfModel.animations.size(); ++i) {
		const auto& gltfAnimation = gltfModel.animations[i];
		auto& animation           = Animations.emplace_back(std::make_shared<Animation>());
		animation->Name           = "Animation " + std::to_string(i);
		if (!gltfAnimation.name.empty()) { animation->Name = gltfAnimation.name; }

		for (const auto& gltfSampler : gltfAnimation.samplers) {
			auto& sampler = animation->Samplers.emplace_back();

			switch (gltfSampler.interpolation) {
				case fastgltf::AnimationInterpolation::Step:
					sampler.Interpolation = AnimationInterpolation::Step;
					break;
				case fastgltf::AnimationInterpolation::CubicSpline:
					sampler.Interpolation = AnimationInterpolation::CubicSpline;
					break;
				case fastgltf::AnimationInterpolation::Linear:
				default:
					sampler.Interpolation = AnimationInterpolation::Linear;
					break;
			}

			// Input data
			{
				const auto& gltfAccessor   = gltfModel.accessors[gltfSampler.inputAccessor];
				const auto& gltfBufferView = gltfModel.bufferViews[gltfAccessor.bufferViewIndex.value()];
				const auto& gltfBuffer     = gltfModel.buffers[gltfBufferView.bufferIndex];
				const auto& gltfBytes      = std::get<fastgltf::sources::Vector>(gltfBuffer.data);
				const float* inputData =
					reinterpret_cast<const float*>(&gltfBytes.bytes[gltfAccessor.byteOffset + gltfBufferView.byteOffset]);
				sampler.Inputs.resize(gltfAccessor.count);
				memcpy(sampler.Inputs.data(), inputData, gltfAccessor.count * sizeof(float));

				for (const float input : sampler.Inputs) {
					animation->StartTime = std::min(animation->StartTime, input);
					animation->EndTime   = std::max(animation->EndTime, input);
				}
			}

			// Output data
			{
				const auto& gltfAccessor   = gltfModel.accessors[gltfSampler.outputAccessor];
				const auto& gltfBufferView = gltfModel.bufferViews[gltfAccessor.bufferViewIndex.value()];
				const auto& gltfBuffer     = gltfModel.buffers[gltfBufferView.bufferIndex];
				const auto& gltfBytes      = std::get<fastgltf::sources::Vector>(gltfBuffer.data);
				const void* outputData     = &gltfBytes.bytes[gltfAccessor.byteOffset + gltfBufferView.byteOffset];
				sampler.Outputs.resize(gltfAccessor.count);

				switch (gltfAccessor.type) {
					case fastgltf::AccessorType::Vec3: {
						const glm::vec3* src = reinterpret_cast<const glm::vec3*>(outputData);
						for (size_t i = 0; i < gltfAccessor.count; ++i) { sampler.Outputs[i] = glm::vec4(src[i], 0.0f); }
						break;
					}

					case fastgltf::AccessorType::Vec4: {
						const glm::vec4* src = reinterpret_cast<const glm::vec4*>(outputData);
						memcpy(sampler.Outputs.data(), src, gltfAccessor.count * sizeof(glm::vec4));
						break;
					}

					default:
						break;
				}
			}
		}

		for (const auto& gltfChannel : gltfAnimation.channels) {
			auto& channel = animation->Channels.emplace_back();

			channel.Sampler = gltfChannel.samplerIndex;
			channel.Target  = _nodes[gltfChannel.nodeIndex].get();
			switch (gltfChannel.path) {
				case fastgltf::AnimationPath::Translation:
					channel.Path = AnimationPath::Translation;
					break;
				case fastgltf::AnimationPath::Rotation:
					channel.Path = AnimationPath::Rotation;
					break;
				case fastgltf::AnimationPath::Scale:
					channel.Path = AnimationPath::Scale;
					break;
				default:
					break;
			}
		}
	}
}

void Model::ImportImages(const fastgltf::Asset& gltfModel,
                         const std::filesystem::path& gltfPath,
                         Luna::Vulkan::Device& device) {
	const auto gltfFolder = gltfPath.parent_path();
	// Quickly iterate over materials to find what format each image should be, Srgb or Unorm.
	std::vector<vk::Format> textureFormats(gltfModel.images.size(), vk::Format::eUndefined);
	const auto EnsureFormat = [&](uint32_t index, vk::Format expected) -> void {
		auto& format = textureFormats[gltfModel.textures[index].imageIndex.value()];
		if (format != vk::Format::eUndefined && format != expected) {
			std::cerr << "[GltfImporter] Texture index " << index << " is used in both Srgb and Unorm contexts!\n";
		}
		format = expected;
	};
	for (size_t i = 0; i < gltfModel.materials.size(); ++i) {
		const auto& gltfMaterial = gltfModel.materials[i];

		if (gltfMaterial.pbrData) {
			const auto& pbr = gltfMaterial.pbrData.value();
			if (pbr.baseColorTexture) { EnsureFormat(pbr.baseColorTexture->textureIndex, vk::Format::eR8G8B8A8Srgb); }
			if (pbr.metallicRoughnessTexture) {
				EnsureFormat(pbr.metallicRoughnessTexture->textureIndex, vk::Format::eR8G8B8A8Unorm);
			}
		}
		if (gltfMaterial.normalTexture) {
			EnsureFormat(gltfMaterial.normalTexture->textureIndex, vk::Format::eR8G8B8A8Unorm);
		}
		if (gltfMaterial.emissiveTexture) {
			EnsureFormat(gltfMaterial.emissiveTexture->textureIndex, vk::Format::eR8G8B8A8Srgb);
		}
		if (gltfMaterial.occlusionTexture) {
			EnsureFormat(gltfMaterial.occlusionTexture->textureIndex, vk::Format::eR8G8B8A8Unorm);
		}
	}
	for (size_t i = 0; i < gltfModel.images.size(); ++i) {
		const auto& gltfImage = gltfModel.images[i];
		if (textureFormats[i] == vk::Format::eUndefined) {
			Images.push_back(0);
			continue;
		}

		std::filesystem::path texturePath = "Texture" + std::to_string(i);
		bool loaded                       = false;
		std::vector<uint8_t> bytes;

		std::visit(Overloaded{
								 [&](auto& arg) {
									 std::cerr << "[GltfLoader] gltfImage.data == " << gltfImage.data.index()
														 << std::endl; /* throw std::runtime_error("Data buffer was not loaded!"); */
								 },
								 [&](const fastgltf::sources::FilePath& filePath) {
									 const auto imagePath           = filePath.path;
									 texturePath                    = imagePath.stem();
									 const std::string imagePathStr = imagePath.string();
									 const char* imagePathC         = imagePathStr.c_str();
									 try {
										 bytes  = ReadFileBinary(imagePath);
										 loaded = true;
									 } catch (const std::exception& e) {
										 std::cerr << "[GltfLoader] Failed to load texture: " << imagePath << "\n\t" << e.what() << "\n";
									 }
								 },
								 [&](const fastgltf::sources::Vector& vector) {
									 bytes  = vector.bytes;
									 loaded = true;
								 },
							 },
		           gltfImage.data);

		if (!loaded) {
			std::cerr << "[GltfLoader] Failed to find data source for texture for image '" << gltfImage.name << "'!\n";
			Images.push_back(0);
			continue;
		}

		int width, height, components;
		stbi_set_flip_vertically_on_load(0);
		stbi_uc* pixels = stbi_load_from_memory(bytes.data(), bytes.size(), &width, &height, &components, STBI_rgb_alpha);
		if (pixels == nullptr) {
			std::cerr << "[GltfLoader] Failed to read texture data: " << stbi_failure_reason() << "\n";
			Images.push_back(0);
			continue;
		}
		const size_t byteSize = width * height * 4;

		auto& image   = Images.emplace_back(new Image());
		image->Format = textureFormats[i];
		image->Size   = glm::uvec2(width, height);

		const Luna::Vulkan::ImageCreateInfo imageCI =
			Luna::Vulkan::ImageCreateInfo::Immutable2D(image->Format, width, height, true);
		const Luna::Vulkan::ImageInitialData initialData = {.Data = pixels};
		image->Image                                     = device.CreateImage(imageCI, &initialData);

		stbi_image_free(pixels);
	}
}

void Model::ImportMaterials(const fastgltf::Asset& gltfModel) {
	const auto UVTransform = [](const fastgltf::TextureInfo& texture) -> glm::mat3 {
		const glm::vec2 uvOffset = glm::make_vec2(texture.uvOffset.data());
		const glm::vec2 uvScale  = glm::make_vec2(texture.uvScale.data());
		const float uvRotation   = -texture.rotation;
		const float uvRotC       = glm::cos(uvRotation);
		const float uvRotS       = glm::sin(uvRotation);

		const glm::mat3 T = glm::mat3(1, 0, 0, 0, 1, 0, uvOffset.x, uvOffset.y, 1);
		const glm::mat3 R = glm::mat3(uvRotC, uvRotS, 0, -uvRotS, uvRotC, 0, 0, 0, 1);
		const glm::mat3 S = glm::mat3(uvScale.x, 0, 0, 0, uvScale.y, 0, 0, 0, 1);

		return T * R * S;
	};

	for (size_t i = 0; i < gltfModel.materials.size(); ++i) {
		const auto& gltfMaterial = gltfModel.materials[i];

		auto& material = Materials.emplace_back(new Material());
		material->Name = gltfMaterial.name;

		if (gltfMaterial.pbrData) {
			const auto& pbr = gltfMaterial.pbrData.value();
			if (pbr.baseColorTexture) {
				material->Albedo          = Textures[pbr.baseColorTexture->textureIndex];
				material->AlbedoUV        = pbr.baseColorTexture->texCoordIndex;
				material->AlbedoTransform = UVTransform(*pbr.baseColorTexture);
			}
			if (pbr.metallicRoughnessTexture) {
				material->PBR          = Textures[pbr.metallicRoughnessTexture->textureIndex];
				material->PBRUV        = pbr.metallicRoughnessTexture->texCoordIndex;
				material->PBRTransform = UVTransform(*pbr.metallicRoughnessTexture);
			}

			material->BaseColorFactor = glm::make_vec4(pbr.baseColorFactor.data());
			material->MetallicFactor  = pbr.metallicFactor;
			material->RoughnessFactor = pbr.roughnessFactor;
		}
		if (gltfMaterial.normalTexture) {
			material->Normal          = Textures[gltfMaterial.normalTexture->textureIndex];
			material->NormalUV        = gltfMaterial.normalTexture->texCoordIndex;
			material->NormalTransform = UVTransform(*gltfMaterial.normalTexture);
		}
		if (gltfMaterial.occlusionTexture) {
			material->Occlusion          = Textures[gltfMaterial.occlusionTexture->textureIndex];
			material->OcclusionUV        = gltfMaterial.occlusionTexture->texCoordIndex;
			material->OcclusionTransform = UVTransform(*gltfMaterial.occlusionTexture);
			material->OcclusionFactor    = gltfMaterial.occlusionTexture->scale;
		}
		if (gltfMaterial.emissiveTexture) {
			material->Emissive          = Textures[gltfMaterial.emissiveTexture->textureIndex];
			material->EmissiveUV        = gltfMaterial.emissiveTexture->texCoordIndex;
			material->EmissiveTransform = UVTransform(*gltfMaterial.emissiveTexture);
		}

		material->EmissiveFactor = glm::make_vec3(gltfMaterial.emissiveFactor.data());

		switch (gltfMaterial.alphaMode) {
			case fastgltf::AlphaMode::Opaque:
			default:
				material->AlphaMode = AlphaMode::Opaque;
				break;
			case fastgltf::AlphaMode::Mask:
				material->AlphaMode = AlphaMode::Mask;
				break;
			case fastgltf::AlphaMode::Blend:
				material->AlphaMode = AlphaMode::Blend;
				break;
		}
		material->AlphaCutoff = gltfMaterial.alphaCutoff;
		material->Sidedness   = gltfMaterial.doubleSided ? Sidedness::Both : Sidedness::Front;
	}

	// Create 1 additional material with nothing but defaults, to be used if any mesh primitive does not specify a
	// material.
	_defaultMaterial = Materials.emplace_back(new Material()).get();
}

static VertexAttributes GetAvailableAttributes(const fastgltf::Primitive& prim) {
	VertexAttributes attr = {};

	for (const auto& [attributeName, attributeValue] : prim.attributes) {
		if (attributeName.compare("POSITION") == 0) { attr |= VertexAttributeBits::Position; }
		if (attributeName.compare("NORMAL") == 0) { attr |= VertexAttributeBits::Normal; }
		if (attributeName.compare("TANGENT") == 0) { attr |= VertexAttributeBits::Tangent; }
		if (attributeName.compare("TEXCOORD_0") == 0) { attr |= VertexAttributeBits::Texcoord0; }
		if (attributeName.compare("TEXCOORD_1") == 0) { attr |= VertexAttributeBits::Texcoord1; }
		if (attributeName.compare("COLOR_0") == 0) { attr |= VertexAttributeBits::Color0; }
		if (attributeName.compare("JOINTS_0") == 0) { attr |= VertexAttributeBits::Joints0; }
		if (attributeName.compare("WEIGHTS_0") == 0) { attr |= VertexAttributeBits::Weights0; }
	}
	if (prim.indicesAccessor.has_value()) { attr |= VertexAttributeBits::Index; }

	return attr;
}

static MeshProcessingSteps GetProcessingSteps(VertexAttributes attributes) {
	MeshProcessingSteps steps = {};

	if (!(attributes & VertexAttributeBits::Normal)) {
		// No normals provided. We must generate flat normals, then generate tangents using MikkTSpace.
		steps |= MeshProcessingStepBits::UnpackVertices;
		steps |= MeshProcessingStepBits::GenerateFlatNormals;
		steps |= MeshProcessingStepBits::GenerateTangentSpace;
		steps |= MeshProcessingStepBits::WeldVertices;
	}
	if (!(attributes & VertexAttributeBits::Tangent)) {
		// No tangents provided. We must generate tangents using MikkTSPace.
		steps |= MeshProcessingStepBits::UnpackVertices;
		steps |= MeshProcessingStepBits::GenerateTangentSpace;
		steps |= MeshProcessingStepBits::WeldVertices;
	}
	if (!(attributes & VertexAttributeBits::Index)) {
		// No indices provided. We will weld the mesh and create our own index buffer.
		steps |= MeshProcessingStepBits::WeldVertices;
	}

	return steps;
}

template <typename Source, typename Destination>
static std::vector<Destination> ConvertAccessorData(const fastgltf::Asset& gltfModel,
                                                    const fastgltf::Accessor& gltfAccessor,
                                                    bool vertexAccessor) {
	static_assert(AccessorType<Destination>::Count > 0, "Unknown type conversion given to ConvertAccessorData");

	using D = typename AccessorType<Destination>::UnderlyingT;

	constexpr auto dstCount   = AccessorType<Destination>::Count;
	constexpr Source srcMax   = std::numeric_limits<Source>::max();
	constexpr bool srcSigned  = std::numeric_limits<Source>::is_signed;
	constexpr auto srcSize    = sizeof(Source);
	constexpr auto attrStride = srcSize * dstCount;
	// Accessors used for vertex data must have each element aligned to 4-byte boundaries.
	constexpr auto vertexStride = attrStride % 4 == 0 ? attrStride : attrStride + 4 - (attrStride % 4);

	const auto count           = gltfAccessor.count;
	const auto normalized      = gltfAccessor.normalized;
	const auto& gltfBufferView = gltfModel.bufferViews[*gltfAccessor.bufferViewIndex];
	const auto& gltfBuffer     = gltfModel.buffers[gltfBufferView.bufferIndex];
	const auto& gltfBytes      = std::get<fastgltf::sources::Vector>(gltfBuffer.data);
	const uint8_t* bufferData  = &gltfBytes.bytes[gltfAccessor.byteOffset + gltfBufferView.byteOffset];
	const auto byteStride      = gltfBufferView.byteStride.value_or(vertexAccessor ? vertexStride : attrStride);

	auto Get = [bufferData, byteStride, normalized](size_t attributeIndex, uint8_t componentIndex) -> D {
		const uint8_t* dataPtr = bufferData + (attributeIndex * byteStride) + (componentIndex * srcSize);
		const Source v         = *reinterpret_cast<const Source*>(dataPtr);

		if (normalized) {
			if (srcSigned) {
				return std::max(static_cast<D>(v) / static_cast<D>(srcMax), static_cast<D>(-1.0));
			} else {
				return static_cast<D>(v) / static_cast<D>(srcMax);
			}
		} else {
			return static_cast<D>(v);
		}
	};

	std::vector<Destination> dst(count);

	if constexpr (dstCount == 1) {
		for (size_t i = 0; i < count; ++i) { dst[i] = static_cast<D>(Get(i, 0)); }
	} else {
		for (size_t i = 0; i < count; ++i) {
			dst[i][0] = static_cast<D>(Get(i, 0));
			dst[i][1] = static_cast<D>(Get(i, 1));
			if constexpr (dstCount >= 3) { dst[i][2] = static_cast<D>(Get(i, 2)); }
			if constexpr (dstCount >= 4) { dst[i][3] = static_cast<D>(Get(i, 3)); }
		}
	}

	return dst;
}

template <typename T>
static std::vector<T> GetAccessorData(const fastgltf::Asset& gltfModel,
                                      const fastgltf::Accessor& gltfAccessor,
                                      bool vertexAccessor = false) {
	constexpr auto outType          = AccessorType<T>::Type;
	constexpr auto outComponentType = AccessorType<T>::Component;
	const auto accessorType         = gltfAccessor.type;

	// Don't allow conversion between mismatching types (e.g. VEC2 to VEC4)
	if (outType == accessorType) {
		switch (gltfAccessor.componentType) {
			case fastgltf::ComponentType::Byte:
				return ConvertAccessorData<int8_t, T>(gltfModel, gltfAccessor, vertexAccessor);
			case fastgltf::ComponentType::UnsignedByte:
				return ConvertAccessorData<uint8_t, T>(gltfModel, gltfAccessor, vertexAccessor);
			case fastgltf::ComponentType::Short:
				return ConvertAccessorData<int16_t, T>(gltfModel, gltfAccessor, vertexAccessor);
			case fastgltf::ComponentType::UnsignedShort:
				return ConvertAccessorData<uint16_t, T>(gltfModel, gltfAccessor, vertexAccessor);
			case fastgltf::ComponentType::UnsignedInt:
				return ConvertAccessorData<uint32_t, T>(gltfModel, gltfAccessor, vertexAccessor);
			case fastgltf::ComponentType::Float:
				return ConvertAccessorData<float, T>(gltfModel, gltfAccessor, vertexAccessor);
			case fastgltf::ComponentType::Double:
				return ConvertAccessorData<double, T>(gltfModel, gltfAccessor, vertexAccessor);
			default:
				break;
		}
	}

	return {};
}

template <typename T>
static std::vector<T> GetAccessorData(const fastgltf::Asset& gltfModel,
                                      const fastgltf::Primitive& gltfPrimitive,
                                      VertexAttributeBits attribute) {
	if (attribute == VertexAttributeBits::Index) {
		if (gltfPrimitive.indicesAccessor.has_value()) {
			return GetAccessorData<T>(gltfModel, gltfModel.accessors[*gltfPrimitive.indicesAccessor]);
		}
	} else {
		auto it = gltfPrimitive.attributes.end();
		switch (attribute) {
			case VertexAttributeBits::Position:
				it = gltfPrimitive.attributes.find("POSITION");
				break;
			case VertexAttributeBits::Normal:
				it = gltfPrimitive.attributes.find("NORMAL");
				break;
			case VertexAttributeBits::Tangent:
				it = gltfPrimitive.attributes.find("TANGENT");
				break;
			case VertexAttributeBits::Texcoord0:
				it = gltfPrimitive.attributes.find("TEXCOORD_0");
				break;
			case VertexAttributeBits::Texcoord1:
				it = gltfPrimitive.attributes.find("TEXCOORD_1");
				break;
			case VertexAttributeBits::Color0:
				it = gltfPrimitive.attributes.find("COLOR_0");
				break;
			case VertexAttributeBits::Joints0:
				it = gltfPrimitive.attributes.find("JOINTS_0");
				break;
			case VertexAttributeBits::Weights0:
				it = gltfPrimitive.attributes.find("WEIGHTS_0");
				break;
			default:
				throw std::runtime_error("Requested unknown vertex attribute!");
				break;
		}

		if (it != gltfPrimitive.attributes.end()) {
			return GetAccessorData<T>(gltfModel, gltfModel.accessors[it->second], true);
		}
	}

	return {};
}

void Model::ImportMeshes(const fastgltf::Asset& gltfModel, Luna::Vulkan::Device& device) {
	// Create a MikkTSpace context for tangent generation.
	SMikkTSpaceContext mikktContext = {.m_pInterface = &MikkTInterface};

	for (size_t meshIndex = 0; meshIndex < gltfModel.meshes.size(); ++meshIndex) {
		const auto& gltfMesh = gltfModel.meshes[meshIndex];
		auto& mesh           = Meshes.emplace_back(new Mesh());
		mesh->Id             = meshIndex;
		mesh->Name           = "Mesh " + std::to_string(meshIndex);
		if (!mesh->Name.empty()) { mesh->Name = gltfMesh.name; }

		// Default material is always appended to the end of the glTF materials array.
		const size_t defaultMaterialIndex = Materials.size() - 1;

		// Sort all of our primitives by material.
		std::vector<fastgltf::Primitive> gltfPrimitives = gltfMesh.primitives;
		std::sort(gltfPrimitives.begin(),
		          gltfPrimitives.end(),
		          [defaultMaterialIndex](const fastgltf::Primitive& a, const fastgltf::Primitive& b) -> bool {
								return a.materialIndex.value_or(defaultMaterialIndex) > b.materialIndex.value_or(defaultMaterialIndex);
							});

		// Determine how many materials we're going to have, based on whether or not we're merging by material.
		std::vector<std::vector<int>> materialPrimitives;  // One entry per material, with a list of primitives.
		if (MergeSubmeshes) {
			// Determine exactly which primitives belong to each material.
			materialPrimitives.resize(Materials.size());
			for (uint32_t i = 0; i < gltfPrimitives.size(); ++i) {
				const auto& gltfPrimitive = gltfPrimitives[i];
				materialPrimitives[gltfPrimitive.materialIndex.value_or(defaultMaterialIndex)].push_back(i);
			}

			// Prune materials with no primitives from our list.
			for (auto it = materialPrimitives.begin(); it != materialPrimitives.end();) {
				if ((*it).empty()) {
					it = materialPrimitives.erase(it);
				} else {
					++it;
				}
			}
		} else {
			materialPrimitives.resize(gltfMesh.primitives.size());
			for (uint32_t i = 0; i < gltfPrimitives.size(); ++i) { materialPrimitives[i].push_back(i); }
		}

		// Start keeping track of how many vertices and indices we've created. We will need to use these when drawing later.
		std::vector<Vertex> meshVertices;
		std::vector<uint32_t> meshIndices;

		for (size_t materialIndex = 0; materialIndex < materialPrimitives.size(); ++materialIndex) {
			const auto& primitiveList = materialPrimitives[materialIndex];
			auto& submesh             = mesh->Submeshes.emplace_back();

			// Since all primitives within the submesh share a material, we simply take the material value from the first
			// primitive.
			const size_t gltfMaterialIndex = gltfPrimitives[primitiveList[0]].materialIndex.value_or(defaultMaterialIndex);
			submesh.Material               = Materials[gltfMaterialIndex].get();

			// Assign the submesh's starting offset.
			submesh.FirstVertex = meshVertices.size();
			submesh.FirstIndex  = meshIndices.size();
			submesh.VertexCount = 0;
			submesh.IndexCount  = 0;

			glm::vec3 boundsMin(std::numeric_limits<float>::max());
			glm::vec3 boundsMax(std::numeric_limits<float>::lowest());

			for (const auto gltfPrimitiveIndex : primitiveList) {
				const auto& gltfPrimitive = gltfPrimitives[gltfPrimitiveIndex];
				const auto primAttributes = GetAvailableAttributes(gltfPrimitive);
				const auto primProcessing = GetProcessingSteps(primAttributes);

				// Verify we have position data.
				if (!(primAttributes & VertexAttributeBits::Position)) { continue; }

				std::vector<Vertex> vertices;
				std::vector<uint32_t> indices;

				// Load the geometry data.
				{
					ProfileTimer loadVertices;

					auto positions  = GetAccessorData<glm::vec3>(gltfModel, gltfPrimitive, VertexAttributeBits::Position);
					auto normals    = GetAccessorData<glm::vec3>(gltfModel, gltfPrimitive, VertexAttributeBits::Normal);
					auto tangents   = GetAccessorData<glm::vec4>(gltfModel, gltfPrimitive, VertexAttributeBits::Tangent);
					auto texcoords0 = GetAccessorData<glm::vec2>(gltfModel, gltfPrimitive, VertexAttributeBits::Texcoord0);
					auto texcoords1 = GetAccessorData<glm::vec2>(gltfModel, gltfPrimitive, VertexAttributeBits::Texcoord1);
					auto colors0    = GetAccessorData<glm::vec4>(gltfModel, gltfPrimitive, VertexAttributeBits::Color0);
					if (colors0.empty()) {
						auto colors0v3 = GetAccessorData<glm::vec3>(gltfModel, gltfPrimitive, VertexAttributeBits::Color0);
						if (!colors0v3.empty()) {
							colors0.reserve(colors0v3.size());
							for (const auto& c : colors0v3) { colors0.push_back(glm::vec4(c, 1.0f)); }
						}
					}
					auto joints0  = GetAccessorData<glm::uvec4>(gltfModel, gltfPrimitive, VertexAttributeBits::Joints0);
					auto weights0 = GetAccessorData<glm::vec4>(gltfModel, gltfPrimitive, VertexAttributeBits::Weights0);

					normals.resize(positions.size());
					tangents.resize(positions.size());
					texcoords0.resize(positions.size());
					texcoords1.resize(positions.size());
					colors0.resize(positions.size(), glm::vec4(1, 1, 1, 1));
					joints0.resize(positions.size());
					weights0.resize(positions.size());

					vertices.reserve(positions.size());
					for (size_t i = 0; i < positions.size(); ++i) {
						vertices.push_back(Vertex{.Position  = positions[i],
						                          .Normal    = normals[i],
						                          .Tangent   = tangents[i],
						                          .Texcoord0 = texcoords0[i],
						                          .Texcoord1 = texcoords1[i],
						                          .Color0    = colors0[i],
						                          .Joints0   = joints0[i],
						                          .Weights0  = weights0[i]});
					}

					indices = GetAccessorData<uint32_t>(gltfModel, gltfPrimitive, VertexAttributeBits::Index);

					_timeVertexLoad += loadVertices.Get();
				}

				// Pre-Processing: Unpack vertices
				if (primProcessing & MeshProcessingStepBits::UnpackVertices) {
					ProfileTimer timeUnpack;

					if (indices.size() > 0) {
						std::vector<Vertex> newVertices(indices.size());

						uint32_t newIndex = 0;
						for (const uint32_t index : indices) { newVertices[newIndex++] = vertices[index]; }
						vertices = std::move(newVertices);
						indices.clear();
					}

					_timeUnpackVertices += timeUnpack.Get();
				}

				// Pre-Processing: Flat normals
				if (primProcessing & MeshProcessingStepBits::GenerateFlatNormals) {
					ProfileTimer timeFlatNormals;

					const size_t faceCount = vertices.size() / 3;

					for (size_t i = 0; i < faceCount; ++i) {
						auto& v1     = vertices[i * 3 + 0];
						auto& v2     = vertices[i * 3 + 1];
						auto& v3     = vertices[i * 3 + 2];
						const auto n = glm::normalize(glm::triangleNormal(v1.Position, v2.Position, v3.Position));

						v1.Normal = n;
						v2.Normal = n;
						v3.Normal = n;
					}

					_timeGenerateFlatNormals += timeFlatNormals.Get();
				}

				// Pre-Processing: Generate tangent space
				if (primProcessing & MeshProcessingStepBits::GenerateTangentSpace) {
					ProfileTimer timeTangent;

					MikkTContext context{vertices, submesh.Material};
					mikktContext.m_pUserData = &context;
					genTangSpaceDefault(&mikktContext);

					_timeGenerateTangents += timeTangent.Get();
				}

				// Pre-Processing: Weld vertices
				if (primProcessing & MeshProcessingStepBits::WeldVertices) {
					ProfileTimer timeWeld;

					indices.clear();
					indices.reserve(vertices.size());
					std::unordered_map<Vertex, uint32_t> uniqueVertices;

					const size_t oldVertexCount = vertices.size();
					uint32_t newVertexCount     = 0;
					for (size_t i = 0; i < oldVertexCount; ++i) {
						const Vertex& v = vertices[i];

						const auto it = uniqueVertices.find(v);
						if (it == uniqueVertices.end()) {
							const uint32_t index = newVertexCount++;
							uniqueVertices.insert(std::make_pair(v, index));
							vertices[index] = v;
							indices.push_back(index);
						} else {
							indices.push_back(it->second);
						}
					}
					vertices.resize(newVertexCount);

					_timeWeldVertices += timeWeld.Get();
				}

				// Calculate primitive bounding box.
				for (const auto& v : vertices) {
					boundsMin = glm::min(v.Position, boundsMin);
					boundsMax = glm::max(v.Position, boundsMax);
				}

				// Post-processing: Offset indices
				for (auto& i : indices) { i += submesh.VertexCount; }

				// Append primitive vertices to mesh
				meshVertices.reserve(meshVertices.size() + vertices.size());
				meshVertices.insert(meshVertices.end(), vertices.begin(), vertices.end());
				meshIndices.reserve(meshIndices.size() + indices.size());
				meshIndices.insert(meshIndices.end(), indices.begin(), indices.end());

				submesh.VertexCount += vertices.size();
				submesh.IndexCount += indices.size();
			}

			submesh.Bounds       = BoundingBox(boundsMin, boundsMax);
			submesh.Bounds.Valid = true;
		}

		mesh->TotalVertexCount = meshVertices.size();
		mesh->TotalIndexCount  = meshIndices.size();

		for (const auto& submesh : mesh->Submeshes) {
			if (submesh.Bounds.Valid && !mesh->Bounds.Valid) {
				mesh->Bounds       = submesh.Bounds;
				mesh->Bounds.Valid = true;
			}
			mesh->Bounds.Min = glm::min(mesh->Bounds.Min, submesh.Bounds.Min);
			mesh->Bounds.Max = glm::min(mesh->Bounds.Max, submesh.Bounds.Max);
		}

		const vk::DeviceSize vertexSize = meshVertices.size() * sizeof(Vertex);
		const vk::DeviceSize indexSize  = meshIndices.size() * sizeof(uint32_t);
		std::vector<uint8_t> bufferData(vertexSize + indexSize);
		memcpy(bufferData.data(), meshVertices.data(), vertexSize);
		memcpy(bufferData.data() + vertexSize, meshIndices.data(), indexSize);
		mesh->IndexOffset = vertexSize;
		const Luna::Vulkan::BufferCreateInfo bufferCI{
			Luna::Vulkan::BufferDomain::Device,
			vertexSize + indexSize,
			vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eIndexBuffer |
				vk::BufferUsageFlagBits::eShaderDeviceAddress |
				vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR | vk::BufferUsageFlagBits::eStorageBuffer};
		mesh->Buffer = device.CreateBuffer(bufferCI, bufferData.data());
	}
}

void Model::ImportNodes(const fastgltf::Asset& gltfModel) {
	const auto& gltfScene = gltfModel.scenes[gltfModel.defaultScene ? gltfModel.defaultScene.value() : 0];

	_nodes.resize(gltfModel.nodes.size());
	for (size_t i = 0; i < gltfModel.nodes.size(); ++i) { _nodes[i] = std::make_shared<Node>(); }

	for (size_t i = 0; i < gltfModel.nodes.size(); ++i) {
		const auto& gltfNode = gltfModel.nodes[i];
		auto& node           = _nodes[i];

		node->Id   = i;
		node->Name = gltfNode.name;
		if (gltfNode.name.empty()) { node->Name = "Node " + std::to_string(i); }

		std::visit(Overloaded{[&](const fastgltf::Node::TRS& trs) {
														node->Translation = glm::make_vec3(trs.translation.data());
														node->Rotation.x  = trs.rotation[0];
														node->Rotation.y  = trs.rotation[1];
														node->Rotation.z  = trs.rotation[2];
														node->Rotation.w  = trs.rotation[3];
														node->Scale       = glm::make_vec3(trs.scale.data());
													},
		                      [&](const fastgltf::Node::TransformMatrix& mat) {
														glm::mat4 matrix = glm::make_mat4(mat.data());
														glm::vec3 skew;
														glm::vec4 perspective;
														glm::decompose(matrix, node->Scale, node->Rotation, node->Translation, skew, perspective);
													}},
		           gltfNode.transform);

		node->Mesh = gltfNode.meshIndex ? Meshes[gltfNode.meshIndex.value()].get() : nullptr;
		node->Skin = gltfNode.skinIndex.value_or(-1);

		for (const auto child : gltfNode.children) {
			node->Children.push_back(_nodes[child].get());
			_nodes[child]->Parent = node.get();
		}
	}

	for (const auto node : gltfScene.nodeIndices) { RootNodes.push_back(_nodes[node].get()); }
}

void Model::ImportSamplers(const fastgltf::Asset& gltfModel, Luna::Vulkan::Device& device) {
	for (size_t i = 0; i < gltfModel.samplers.size(); ++i) {
		const auto& gltfSampler = gltfModel.samplers[i];
		auto& sampler           = Samplers.emplace_back(std::make_shared<Sampler>());

		Luna::Vulkan::SamplerCreateInfo samplerCI{
			.MagFilter        = vk::Filter::eLinear,
			.MinFilter        = vk::Filter::eLinear,
			.MipmapMode       = vk::SamplerMipmapMode::eLinear,
			.AddressModeU     = vk::SamplerAddressMode::eRepeat,
			.AddressModeV     = vk::SamplerAddressMode::eRepeat,
			.AnisotropyEnable = device.GetDeviceInfo().EnabledFeatures.Core.samplerAnisotropy,
			.MaxAnisotropy    = device.GetDeviceInfo().Properties.Core.limits.maxSamplerAnisotropy,
			.MinLod           = 0.0f,
			.MaxLod           = 16.0f};

		if (gltfSampler.magFilter.has_value()) {
			switch (gltfSampler.magFilter.value()) {
				case fastgltf::Filter::Linear:
					samplerCI.MagFilter = vk::Filter::eLinear;
					break;

				case fastgltf::Filter::Nearest:
				default:
					samplerCI.MagFilter = vk::Filter::eNearest;
					break;
			}
		}
		if (gltfSampler.minFilter.has_value()) {
			switch (gltfSampler.minFilter.value()) {
				case fastgltf::Filter::LinearMipMapNearest:
				case fastgltf::Filter::Linear:
					samplerCI.MinFilter  = vk::Filter::eLinear;
					samplerCI.MipmapMode = vk::SamplerMipmapMode::eNearest;
					break;

				case fastgltf::Filter::LinearMipMapLinear:
					samplerCI.MinFilter  = vk::Filter::eLinear;
					samplerCI.MipmapMode = vk::SamplerMipmapMode::eLinear;
					break;

				case fastgltf::Filter::NearestMipMapLinear:
					samplerCI.MinFilter  = vk::Filter::eNearest;
					samplerCI.MipmapMode = vk::SamplerMipmapMode::eLinear;
					break;

				case fastgltf::Filter::NearestMipMapNearest:
				case fastgltf::Filter::Nearest:
				default:
					samplerCI.MinFilter  = vk::Filter::eNearest;
					samplerCI.MipmapMode = vk::SamplerMipmapMode::eNearest;
			}

			switch (gltfSampler.minFilter.value()) {
				case fastgltf::Filter::LinearMipMapNearest:
				case fastgltf::Filter::LinearMipMapLinear:
				case fastgltf::Filter::NearestMipMapLinear:
				case fastgltf::Filter::NearestMipMapNearest:
					samplerCI.MaxLod = 16.0f;
					break;

				default:
					samplerCI.AnisotropyEnable = VK_FALSE;
					samplerCI.MaxLod           = 0.0f;
					break;
			}
		}
		switch (gltfSampler.wrapS) {
			case fastgltf::Wrap::ClampToEdge:
				samplerCI.AddressModeU = vk::SamplerAddressMode::eClampToEdge;
				break;
			case fastgltf::Wrap::MirroredRepeat:
				samplerCI.AddressModeU = vk::SamplerAddressMode::eMirroredRepeat;
				break;
			case fastgltf::Wrap::Repeat:
			default:
				samplerCI.AddressModeU = vk::SamplerAddressMode::eRepeat;
				break;
		}
		switch (gltfSampler.wrapT) {
			case fastgltf::Wrap::ClampToEdge:
				samplerCI.AddressModeV = vk::SamplerAddressMode::eClampToEdge;
				break;
			case fastgltf::Wrap::MirroredRepeat:
				samplerCI.AddressModeV = vk::SamplerAddressMode::eMirroredRepeat;
				break;
			case fastgltf::Wrap::Repeat:
			default:
				samplerCI.AddressModeV = vk::SamplerAddressMode::eRepeat;
				break;
		}

		sampler->Sampler = device.RequestSampler(samplerCI);
	}

	DefaultSampler = Samplers.emplace_back(new Sampler()).get();
	const Luna::Vulkan::SamplerCreateInfo samplerCI{
		.MagFilter        = vk::Filter::eLinear,
		.MinFilter        = vk::Filter::eLinear,
		.MipmapMode       = vk::SamplerMipmapMode::eLinear,
		.AddressModeU     = vk::SamplerAddressMode::eRepeat,
		.AddressModeV     = vk::SamplerAddressMode::eRepeat,
		.AnisotropyEnable = device.GetDeviceInfo().EnabledFeatures.Core.samplerAnisotropy,
		.MaxAnisotropy    = device.GetDeviceInfo().Properties.Core.limits.maxSamplerAnisotropy,
		.MinLod           = 0.0f,
		.MaxLod           = 16.0f};
	DefaultSampler->Sampler = device.RequestSampler(samplerCI);
}

void Model::ImportSkins(const fastgltf::Asset& gltfModel, Luna::Vulkan::Device& device) {
	for (size_t i = 0; i < gltfModel.skins.size(); ++i) {
		const auto& gltfSkin = gltfModel.skins[i];
		auto& skin           = Skins.emplace_back(std::make_shared<Skin>());

		skin->RootNode = _nodes[gltfSkin.skeleton.value()].get();
		for (auto j : gltfSkin.joints) { skin->Joints.push_back(_nodes[j].get()); }
		if (gltfSkin.inverseBindMatrices) {
			const auto& gltfAccessor   = gltfModel.accessors[gltfSkin.inverseBindMatrices.value()];
			const auto& gltfBufferView = gltfModel.bufferViews[gltfAccessor.bufferViewIndex.value()];
			const auto& gltfBuffer     = gltfModel.buffers[gltfBufferView.bufferIndex];
			const auto& gltfBytes      = std::get<fastgltf::sources::Vector>(gltfBuffer.data);
			const glm::mat4* matrices =
				reinterpret_cast<const glm::mat4*>(&gltfBytes.bytes[gltfAccessor.byteOffset + gltfBufferView.byteOffset]);
			skin->InverseBindMatrices.resize(gltfAccessor.count);
			memcpy(skin->InverseBindMatrices.data(), matrices, gltfAccessor.count * sizeof(glm::mat4));

			skin->Buffer = device.CreateBuffer(Luna::Vulkan::BufferCreateInfo{Luna::Vulkan::BufferDomain::Host,
			                                                                  gltfAccessor.count * sizeof(glm::mat4),
			                                                                  vk::BufferUsageFlagBits::eStorageBuffer},
			                                   skin->InverseBindMatrices.data());
		}
	}
}

void Model::ImportTextures(const fastgltf::Asset& gltfModel) {
	for (size_t i = 0; i < gltfModel.textures.size(); ++i) {
		const auto& gltfTexture = gltfModel.textures[i];
		auto& texture           = Textures.emplace_back(std::make_shared<Texture>());

		texture->Image = Images[gltfTexture.imageIndex.value()].get();
		if (gltfTexture.samplerIndex) {
			texture->Sampler = Samplers[gltfTexture.samplerIndex.value()].get();
		} else {
			texture->Sampler = DefaultSampler;
		}
	}
}
