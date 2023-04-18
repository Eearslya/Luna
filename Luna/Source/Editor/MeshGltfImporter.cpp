#include <Luna/Assets/AssetManager.hpp>
#include <Luna/Assets/Mesh.hpp>
#include <Luna/Editor/MeshGltfImporter.hpp>
#include <Luna/Platform/Filesystem.hpp>
#include <Luna/Utility/Path.hpp>
#include <fastgltf_parser.hpp>
#include <fastgltf_util.hpp>

namespace Luna {
template <class... Ts>
struct Overloaded : Ts... {
	using Ts::operator()...;
};
template <class... Ts>
Overloaded(Ts...) -> Overloaded<Ts...>;

struct Buffer {
	std::vector<uint8_t> Data;
};

struct GltfContext {
	Path SourcePath;
	Path GltfPath;
	Path GltfFolder;
	Path AssetFolder;
	std::unique_ptr<fastgltf::Asset> Asset;
	std::vector<Buffer> Buffers;
};

static bool ParseGltf(GltfContext& context);
static bool LoadBuffers(GltfContext& context);
static bool LoadMeshes(GltfContext& context);

bool MeshGltfImporter::Import(const Path& sourcePath) {
	GltfContext context;
	context.SourcePath = sourcePath;
	context.GltfPath   = "project://" + sourcePath.String();
	context.GltfFolder = context.GltfPath.BaseDirectory();

	auto sourceFolder   = std::filesystem::path(sourcePath.String()).parent_path();
	context.AssetFolder = Path("Assets" / sourceFolder.lexically_relative("/Sources"));

	if (!ParseGltf(context)) { return false; }
	if (!LoadBuffers(context)) { return false; }
	if (!LoadMeshes(context)) { return false; }

	return true;
}

bool ParseGltf(GltfContext& context) {
	auto gltfMapping = Filesystem::OpenReadOnlyMapping(context.GltfPath);
	if (!gltfMapping) { return false; }

	fastgltf::GltfDataBuffer gltfData;
	gltfData.copyBytes(gltfMapping->Data<uint8_t>(), gltfMapping->GetSize());
	gltfMapping.Reset();

	fastgltf::GltfType gltfType = fastgltf::determineGltfFileType(&gltfData);
	if (gltfType == fastgltf::GltfType::Invalid) { return false; }

	fastgltf::Parser parser;
	const fastgltf::Options options = fastgltf::Options::LoadGLBBuffers | fastgltf::Options::DecomposeNodeMatrices;
	std::unique_ptr<fastgltf::glTF> loaded;
	if (gltfType == fastgltf::GltfType::glTF) {
		loaded = parser.loadGLTF(&gltfData, "", options);
	} else {
		loaded = parser.loadBinaryGLTF(&gltfData, "", options);
	}

	fastgltf::Error loadError = fastgltf::Error::None;
	loadError                 = parser.getError();
	if (loadError != fastgltf::Error::None) { return false; }
	loadError = loaded->parse(fastgltf::Category::All);
	if (loadError != fastgltf::Error::None) { return false; }
	loadError = loaded->validate();
	if (loadError != fastgltf::Error::None) { return false; }

	context.Asset = loaded->getParsedAsset();

	context.Buffers.resize(context.Asset->buffers.size());

	return true;
}

bool LoadBuffers(GltfContext& context) {
	const size_t bufferCount = context.Asset->buffers.size();

	for (size_t i = 0; i < bufferCount; ++i) {
		const auto& gltfBuffer = context.Asset->buffers[i];
		auto& buffer           = context.Buffers[i];

		std::visit(Overloaded{
								 [](auto& arg) {},
								 [&](const fastgltf::sources::Vector& vector) { buffer.Data = vector.bytes; },
								 [&](const fastgltf::sources::URI& uri) {
									 const Path path = context.GltfFolder / std::string(uri.uri.path());
									 auto map        = Filesystem::OpenReadOnlyMapping(path);
									 if (!map) { return; }
									 const uint8_t* dataStart = map->Data<uint8_t>() + uri.fileByteOffset;
									 buffer.Data              = {dataStart, dataStart + gltfBuffer.byteLength};
								 },
							 },
		           gltfBuffer.data);

		if (buffer.Data.size() == 0) { return false; }
	}

	return true;
}

bool LoadMeshes(GltfContext& context) {
	for (uint32_t i = 0; i < context.Asset->meshes.size(); ++i) {
		const auto& gltfMesh = context.Asset->meshes[i];

		auto meshName = "Mesh " + std::to_string(i);
		if (!gltfMesh.name.empty()) { meshName = gltfMesh.name; }
		auto mesh = AssetManager::CreateAsset<Mesh>(context.AssetFolder / "Meshes" / (meshName + ".lmesh"));

		AssetManager::SaveAsset(AssetManager::GetAssetMetadata(mesh->Handle), mesh);
	}

	return true;
}
}  // namespace Luna
