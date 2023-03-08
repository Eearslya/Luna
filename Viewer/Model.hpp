#pragma once

#include <Luna/Utility/Hash.hpp>
#include <Luna/Vulkan/Common.hpp>
#include <chrono>
#include <filesystem>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace fastgltf {
class Asset;
class Mesh;
};  // namespace fastgltf

enum class AlphaMode { Opaque, Mask, Blend };
enum class AnimationInterpolation { Linear, Step, CubicSpline };
enum class AnimationPath { Translation, Rotation, Scale, Weights };
enum class Sidedness { Front, Back, Both };

struct BoundingBox {
	BoundingBox() = default;

	BoundingBox(const glm::vec3& min, const glm::vec3& max) : Min(glm::min(min, max)), Max(glm::max(min, max)) {}

	BoundingBox Transform(const glm::mat4& m) const {
		glm::vec3 min = glm::vec3(m[3]);
		glm::vec3 max = min;
		glm::vec3 v0, v1;

		const glm::vec3 right = glm::vec3(m[0]);
		v0                    = right * Min.x;
		v1                    = right * Max.x;
		min += glm::min(v0, v1);
		max += glm::max(v0, v1);

		const glm::vec3 up = glm::vec3(m[1]);
		v0                 = up * Min.y;
		v1                 = up * Max.y;
		min += glm::min(v0, v1);
		max += glm::max(v0, v1);

		const glm::vec3 back = glm::vec3(m[2]);
		v0                   = back * Min.z;
		v1                   = back * Max.z;
		min += glm::min(v0, v1);
		max += glm::max(v0, v1);

		return BoundingBox(min, max);
	}

	glm::vec3 Min;
	glm::vec3 Max;
	bool Valid = false;
};

struct Vertex {
	glm::vec3 Position;
	glm::vec3 Normal;
	glm::vec4 Tangent;
	glm::vec2 Texcoord0;
	glm::vec2 Texcoord1;
	glm::vec4 Color0;
	glm::uvec4 Joints0;
	glm::vec4 Weights0;

	bool operator==(const Vertex& other) const {
		return Position == other.Position && Normal == other.Normal && Tangent == other.Tangent &&
		       Texcoord0 == other.Texcoord0 && Texcoord1 == other.Texcoord1 && Color0 == other.Color0 &&
		       Joints0 == other.Joints0 && Weights0 == other.Weights0;
	}
};

template <>
struct std::hash<Vertex> {
	size_t operator()(const Vertex& data) const {
		Luna::Hasher h;
		h.Data(sizeof(data.Position), glm::value_ptr(data.Position));
		h.Data(sizeof(data.Normal), glm::value_ptr(data.Normal));
		h.Data(sizeof(data.Tangent), glm::value_ptr(data.Tangent));
		h.Data(sizeof(data.Texcoord0), glm::value_ptr(data.Texcoord0));
		h.Data(sizeof(data.Texcoord1), glm::value_ptr(data.Texcoord1));
		h.Data(sizeof(data.Color0), glm::value_ptr(data.Color0));
		h.Data(sizeof(data.Joints0), glm::value_ptr(data.Joints0));
		h.Data(sizeof(data.Weights0), glm::value_ptr(data.Weights0));
		return static_cast<size_t>(h.Get());
	}
};

struct Image {
	vk::Format Format;
	Luna::Vulkan::ImageHandle Image;
	glm::uvec2 Size;
};

struct Sampler {
	Luna::Vulkan::Sampler* Sampler;
};

struct Texture {
	Image* Image       = nullptr;
	Sampler* Sampler   = nullptr;
	int32_t BoundIndex = -1;
};

struct MaterialData {
	alignas(16) glm::mat4 AlbedoTransform    = glm::mat4(1.0f);
	alignas(16) glm::mat4 NormalTransform    = glm::mat4(1.0f);
	alignas(16) glm::mat4 PBRTransform       = glm::mat4(1.0f);
	alignas(16) glm::mat4 OcclusionTransform = glm::mat4(1.0f);
	alignas(16) glm::mat4 EmissiveTransform  = glm::mat4(1.0f);

	alignas(16) glm::vec4 BaseColorFactor = glm::vec4(1, 1, 1, 1);
	alignas(16) glm::vec4 EmissiveFactor  = glm::vec4(0, 0, 0, 0);

	int AlbedoIndex       = -1;
	int NormalIndex       = -1;
	int PBRIndex          = -1;
	int OcclusionIndex    = -1;
	int EmissiveIndex     = -1;
	int AlbedoUV          = -1;
	int NormalUV          = -1;
	int PBRUV             = -1;
	int OcclusionUV       = -1;
	int EmissiveUV        = -1;
	int DoubleSided       = 0;
	int AlphaMode         = 0;
	float AlphaCutoff     = 0.0f;
	float MetallicFactor  = 0.0f;
	float RoughnessFactor = 1.0f;
	float OcclusionFactor = 1.0f;
};

template <>
struct std::hash<MaterialData> {
	size_t operator()(const MaterialData& data) {
		Luna::Hasher h;
		h.Data(sizeof(glm::mat4), glm::value_ptr(data.AlbedoTransform));
		h.Data(sizeof(glm::mat4), glm::value_ptr(data.NormalTransform));
		h.Data(sizeof(glm::mat4), glm::value_ptr(data.PBRTransform));
		h.Data(sizeof(glm::mat4), glm::value_ptr(data.OcclusionTransform));
		h.Data(sizeof(glm::mat4), glm::value_ptr(data.EmissiveTransform));

		h.Data(sizeof(glm::vec4), glm::value_ptr(data.BaseColorFactor));
		h.Data(sizeof(glm::vec4), glm::value_ptr(data.EmissiveFactor));

		h(data.AlbedoUV);
		h(data.NormalUV);
		h(data.PBRUV);
		h(data.OcclusionUV);
		h(data.EmissiveUV);
		h(data.DoubleSided);
		h(data.AlphaMode);
		h(data.AlphaCutoff);
		h(data.MetallicFactor);
		h(data.RoughnessFactor);
		h(data.OcclusionFactor);
		return static_cast<size_t>(h.Get());
	}
};

struct Material {
	void Update(Luna::Vulkan::Device& device) const;

	std::string Name;
	glm::vec4 BaseColorFactor = glm::vec4(1, 1, 1, 1);
	glm::vec3 EmissiveFactor  = glm::vec3(0, 0, 0);
	std::shared_ptr<Texture> Albedo;
	std::shared_ptr<Texture> Normal;
	std::shared_ptr<Texture> PBR;
	std::shared_ptr<Texture> Occlusion;
	std::shared_ptr<Texture> Emissive;
	uint32_t AlbedoUV            = 0;
	uint32_t NormalUV            = 0;
	uint32_t PBRUV               = 0;
	uint32_t OcclusionUV         = 0;
	uint32_t EmissiveUV          = 0;
	glm::mat3 AlbedoTransform    = glm::mat3(1.0f);
	glm::mat3 NormalTransform    = glm::mat3(1.0f);
	glm::mat3 PBRTransform       = glm::mat3(1.0f);
	glm::mat3 OcclusionTransform = glm::mat3(1.0f);
	glm::mat3 EmissiveTransform  = glm::mat3(1.0f);
	AlphaMode AlphaMode          = AlphaMode::Opaque;
	float AlphaCutoff            = 0.5f;
	float MetallicFactor         = 1.0f;
	float RoughnessFactor        = 1.0f;
	float OcclusionFactor        = 1.0f;
	Sidedness Sidedness          = Sidedness::Front;

	mutable MaterialData Data;
	mutable Luna::Vulkan::BufferHandle DataBuffer;
	mutable Luna::Hash DataHash = {};
};

struct Submesh {
	Material* Material         = nullptr;
	vk::DeviceSize VertexCount = 0;
	vk::DeviceSize IndexCount  = 0;
	vk::DeviceSize FirstVertex = 0;
	vk::DeviceSize FirstIndex  = 0;
	BoundingBox Bounds;
};

struct VertexAttribute {
	vk::DeviceSize Offset;
	vk::DeviceSize Size;
	vk::DeviceSize Stride;
};

struct Mesh {
	uint32_t Id;
	std::string Name;
	std::vector<Submesh> Submeshes;
	Luna::Vulkan::BufferHandle Buffer;
	BoundingBox Bounds;

	VertexAttribute Position;
	VertexAttribute Normal;
	VertexAttribute Tangent;
	VertexAttribute Bitangent;
	VertexAttribute Texcoord0;
	VertexAttribute Joints0;
	VertexAttribute Weights0;
	VertexAttribute Index;

	vk::DeviceSize PositionOffset   = 0;
	vk::DeviceSize NormalOffset     = 0;
	vk::DeviceSize TangentOffset    = 0;
	vk::DeviceSize BitangentOffset  = 0;
	vk::DeviceSize Texcoord0Offset  = 0;
	vk::DeviceSize Joints0Offset    = 0;
	vk::DeviceSize Weights0Offset   = 0;
	vk::DeviceSize IndexOffset      = 0;
	vk::DeviceSize TotalVertexCount = 0;
	vk::DeviceSize TotalIndexCount  = 0;
};

struct Node {
	uint32_t Id = 0;
	std::string Name;
	Node* Parent = nullptr;
	std::vector<Node*> Children;
	Mesh* Mesh   = nullptr;
	int64_t Skin = -1;
	BoundingBox AABB;
	BoundingBox BVH;

	glm::vec3 Translation = glm::vec3(0.0f);
	glm::quat Rotation    = glm::quat();
	glm::vec3 Scale       = glm::vec3(1.0f);

	glm::vec3 AnimTranslation = glm::vec3(0.0f);
	glm::quat AnimRotation    = glm::quat();
	glm::vec3 AnimScale       = glm::vec3(1.0f);

	glm::mat4 GetLocalTransform() const {
		return glm::translate(glm::mat4(1.0f), Translation) * glm::mat4(Rotation) * glm::scale(glm::mat4(1.0f), Scale);
	}
	glm::mat4 GetGlobalTransform() const {
		glm::mat4 matrix = GetLocalTransform();

		Node* parent = Parent;
		while (parent) {
			matrix = parent->GetLocalTransform() * matrix;
			parent = parent->Parent;
		}

		return matrix;
	}
	glm::mat4 GetAnimLocalTransform() const {
		return glm::translate(glm::mat4(1.0f), AnimTranslation) * glm::mat4(AnimRotation) *
		       glm::scale(glm::mat4(1.0f), AnimScale);
	}
	glm::mat4 GetAnimGlobalTransform() const {
		glm::mat4 matrix = GetAnimLocalTransform();

		Node* parent = Parent;
		while (parent) {
			matrix = parent->GetAnimLocalTransform() * matrix;
			parent = parent->Parent;
		}

		return matrix;
	}
	void ResetAnimation() {
		AnimTranslation = Translation;
		AnimRotation    = Rotation;
		AnimScale       = Scale;
	}
};

struct Skin {
	Luna::Vulkan::BufferHandle Buffer;
	Node* RootNode = nullptr;
	std::vector<Node*> Joints;
	std::vector<glm::mat4> InverseBindMatrices;
};

struct AnimationSampler {
	AnimationInterpolation Interpolation = AnimationInterpolation::Linear;
	std::vector<float> Inputs;
	std::vector<glm::vec4> Outputs;
};

struct AnimationChannel {
	AnimationPath Path = AnimationPath::Translation;
	Node* Target       = nullptr;
	uint32_t Sampler   = 0;
};

struct Animation {
	std::string Name;
	float StartTime = 0.0f;
	float EndTime   = 0.0f;
	std::vector<AnimationChannel> Channels;
	std::vector<AnimationSampler> Samplers;
};

struct ProfileTimer {
	ProfileTimer() {
		Reset();
	}

	double Get() const {
		const auto now     = std::chrono::high_resolution_clock::now();
		const auto elapsed = now - StartTime;
		return static_cast<double>(std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count()) / 1000000.0;
	}

	void Reset() {
		StartTime = std::chrono::high_resolution_clock::now();
	}

	std::chrono::high_resolution_clock::time_point StartTime;
};

class Model {
 public:
	Model(Luna::Vulkan::Device& device, const std::filesystem::path& gltfPath);

	void ResetAnimation();

	std::string Name;
	glm::mat4 AABB;
	std::vector<std::shared_ptr<Animation>> Animations;
	std::vector<std::shared_ptr<Image>> Images;
	std::vector<std::shared_ptr<Material>> Materials;
	std::vector<std::shared_ptr<Mesh>> Meshes;
	std::vector<std::vector<Material*>> MeshMaterials;
	std::vector<Node*> RootNodes;
	std::vector<std::shared_ptr<Sampler>> Samplers;
	std::vector<std::shared_ptr<Skin>> Skins;
	std::vector<std::shared_ptr<Texture>> Textures;

	bool Animate             = true;
	uint32_t ActiveAnimation = 0;
	Sampler* DefaultSampler  = nullptr;

 private:
	void CalculateBounds(Node* node, Node* parent);
	void ImportAnimations(const fastgltf::Asset& gltfModel);
	void ImportImages(const fastgltf::Asset& gltfModel,
	                  const std::filesystem::path& gltfPath,
	                  Luna::Vulkan::Device& device);
	void ImportMaterials(const fastgltf::Asset& gltfModel);
	void ImportMeshes(const fastgltf::Asset& gltfModel, Luna::Vulkan::Device& device);
	void ImportNodes(const fastgltf::Asset& gltfModel);
	void ImportSamplers(const fastgltf::Asset& gltfModel, Luna::Vulkan::Device& device);
	void ImportSkins(const fastgltf::Asset& gltfModel, Luna::Vulkan::Device& device);
	void ImportTextures(const fastgltf::Asset& gltfModel);

	void ImportMesh(const fastgltf::Asset& gltfModel, const fastgltf::Mesh& gltfMesh, Mesh& mesh);

	Material* _defaultMaterial = nullptr;
	std::vector<std::shared_ptr<Node>> _nodes;
	glm::vec3 _minDim = glm::vec3(std::numeric_limits<float>::max());
	glm::vec3 _maxDim = glm::vec3(std::numeric_limits<float>::lowest());

	double _timeParse               = 0.0;
	double _timeBufferLoad          = 0.0;
	double _timeMeshLoad            = 0.0;
	double _timeVertexLoad          = 0.0;
	double _timeUnpackVertices      = 0.0;
	double _timeGenerateFlatNormals = 0.0;
	double _timeGenerateTangents    = 0.0;
	double _timeWeldVertices        = 0.0;
};
