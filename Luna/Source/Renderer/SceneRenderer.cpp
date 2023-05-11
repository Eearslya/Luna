#include <Luna/Assets/AssetManager.hpp>
#include <Luna/Assets/Material.hpp>
#include <Luna/Assets/Mesh.hpp>
#include <Luna/Editor/Editor.hpp>
#include <Luna/Renderer/RenderContext.hpp>
#include <Luna/Renderer/RenderRunner.hpp>
#include <Luna/Renderer/Renderer.hpp>
#include <Luna/Renderer/SceneRenderer.hpp>
#include <Luna/Renderer/ShaderManager.hpp>
#include <Luna/Renderer/StaticMesh.hpp>
#include <Luna/Scene/Entity.hpp>
#include <Luna/Scene/MeshRendererComponent.hpp>
#include <Luna/Scene/Scene.hpp>
#include <Luna/Scene/TransformComponent.hpp>
#include <Luna/Vulkan/CommandBuffer.hpp>
#include <Luna/Vulkan/Image.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace Luna {
struct CameraData {
	glm::mat4 ViewProjection;
	glm::mat4 InvViewProjection;
	glm::mat4 Projection;
	glm::mat4 InvProjection;
	glm::mat4 View;
	glm::mat4 InvView;
	glm::vec3 CameraPosition;
	float ZNear;
	float ZFar;
};

SceneRenderer::SceneRenderer(const RenderContext& context, SceneRendererFlags flags)
		: _context(context), _flags(flags) {}

bool SceneRenderer::GetClearColor(uint32_t attachment, vk::ClearColorValue* value) const {
	if (value) { *value = vk::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f); }

	return true;
}

bool SceneRenderer::GetClearDepthStencil(vk::ClearDepthStencilValue* value) const {
	if (value) { *value = vk::ClearDepthStencilValue(1.0f, 0); }

	return true;
}

void SceneRenderer::BuildRenderPass(Vulkan::CommandBuffer& cmd) {
	CameraData* cam        = cmd.AllocateTypedUniformData<CameraData>(0, 0, 1);
	cam->Projection        = _context.Projection();
	cam->View              = _context.View();
	cam->ViewProjection    = _context.ViewProjection();
	cam->InvProjection     = _context.InverseProjection();
	cam->InvView           = _context.InverseView();
	cam->InvViewProjection = _context.InverseViewProjection();
	cam->CameraPosition    = _context.Position();
	cam->ZNear             = _context.ZNear();
	cam->ZFar              = _context.ZFar();

	const auto& registry = Editor::GetActiveScene().GetRegistry();
	const auto& view     = registry.view<MeshRendererComponent>();
	if (false && view.size() > 0) {
		auto shader =
			ShaderManager::RegisterGraphics("res://Shaders/DummyTri.vert.glsl", "res://Shaders/DummyTri.frag.glsl");
		auto variant = shader->RegisterVariant();
		auto program = variant->GetProgram();

		Material defaultMaterial;

		cmd.SetOpaqueState();
		cmd.SetProgram(program);
		cmd.SetVertexAttribute(0, 0, vk::Format::eR32G32B32Sfloat, 0);
		cmd.SetVertexAttribute(1, 1, vk::Format::eR32G32B32Sfloat, offsetof(Mesh::Vertex, Normal));
		cmd.SetVertexAttribute(2, 1, vk::Format::eR32G32B32A32Sfloat, offsetof(Mesh::Vertex, Tangent));
		cmd.SetVertexAttribute(3, 1, vk::Format::eR32G32Sfloat, offsetof(Mesh::Vertex, Texcoord0));
		cmd.SetVertexAttribute(4, 1, vk::Format::eR32G32Sfloat, offsetof(Mesh::Vertex, Texcoord1));
		cmd.SetVertexAttribute(5, 1, vk::Format::eR32G32B32Sfloat, offsetof(Mesh::Vertex, Color0));

		for (const auto entityId : view) {
			const Entity entity(entityId, Editor::GetActiveScene());
			const auto transform      = entity.GetGlobalTransform();
			const auto& cMeshRenderer = entity.GetComponent<MeshRendererComponent>();

			IntrusivePtr<Mesh> mesh = AssetManager::GetAsset<Mesh>(cMeshRenderer.MeshAsset);
			if (!mesh || !mesh->PositionBuffer) { continue; }

			cmd.SetVertexBinding(0, *mesh->PositionBuffer, 0, 12, vk::VertexInputRate::eVertex);
			cmd.SetVertexBinding(1, *mesh->AttributeBuffer, 0, sizeof(Mesh::Vertex), vk::VertexInputRate::eVertex);
			cmd.SetIndexBuffer(*mesh->PositionBuffer, mesh->TotalVertexCount * 12, vk::IndexType::eUint32);
			cmd.PushConstants(glm::value_ptr(transform), 0, sizeof(transform));

			for (size_t i = 0; i < mesh->Submeshes.size(); ++i) {
				const auto& submesh             = mesh->Submeshes[i];
				Material* material              = &defaultMaterial;
				IntrusivePtr<Material> matAsset = AssetManager::GetAsset<Material>(cMeshRenderer.MaterialAssets[i]);
				if (matAsset) { material = matAsset.Get(); }

				auto* materialData = cmd.AllocateTypedUniformData<Material::MaterialData>(1, 0, 1);
				*materialData      = material->Data();

				cmd.DrawIndexed(submesh.IndexCount, 1, submesh.FirstIndex, submesh.FirstVertex, 0);
			}
		}
	}

	if (_flags & SceneRendererFlagBits::ForwardZPrePass) {
		Renderer::GetRunner(Luna::RendererSuiteType::PrepassDepth)
			.Flush(cmd, _depthQueue, _context, Luna::RendererFlushFlagBits::NoColor);
	}

	if (_flags & SceneRendererFlagBits::ForwardOpaque) {
		Luna::RendererFlushFlags flush = {};
		if (_flags & SceneRendererFlagBits::ForwardZPrePass) {
			flush |= Luna::RendererFlushFlagBits::DepthStencilReadOnly | Luna::RendererFlushFlagBits::DepthTestEqual;
		}
		Renderer::GetRunner(Luna::RendererSuiteType::ForwardOpaque).Flush(cmd, _opaqueQueue, _context, flush);
	}

	// XZ Grid
	{
		auto shader = ShaderManager::RegisterGraphics("res://Shaders/Fullscreen.vert.glsl", "res://Shaders/Grid.frag.glsl");
		auto variant = shader->RegisterVariant();
		auto program = variant->GetProgram();

		cmd.SetOpaqueState();
		cmd.SetProgram(program);
		cmd.SetCullMode(vk::CullModeFlagBits::eNone);
		cmd.SetBlendEnable(true);
		cmd.SetAlphaBlend(vk::BlendFactor::eZero, vk::BlendOp::eAdd, vk::BlendFactor::eOne);
		cmd.SetColorBlend(vk::BlendFactor::eSrcAlpha, vk::BlendOp::eAdd, vk::BlendFactor::eOneMinusSrcAlpha);
		cmd.Draw(3);
	}
}

void SceneRenderer::EnqueuePrepareRenderPass(RenderGraph& graph, TaskComposer& composer) {
	auto& setup = composer.BeginPipelineStage();
	setup.Enqueue([&]() {
		_opaqueVisible.clear();
		_transparentVisible.clear();

		if (_flags & SceneRendererFlagBits::ForwardZPrePass) {
			Renderer::GetRunner(Luna::RendererSuiteType::PrepassDepth).Begin(_depthQueue);
		} else if (_flags & SceneRendererFlagBits::Depth) {
		}

		if (_flags & SceneRendererFlagBits::ForwardOpaque) {
			Renderer::GetRunner(Luna::RendererSuiteType::ForwardOpaque).Begin(_opaqueQueue);
		} else if (_flags & SceneRendererFlagBits::DeferredGBuffer) {
			Renderer::GetRunner(Luna::RendererSuiteType::Deferred).Begin(_opaqueQueue);
		}

		if (_flags & SceneRendererFlagBits::ForwardTransparent) {
			Renderer::GetRunner(Luna::RendererSuiteType::ForwardTransparent).Begin(_transparentQueue);
		}
	});

	if (_flags & (SceneRendererFlagBits::ForwardOpaque | SceneRendererFlagBits::ForwardZPrePass)) {
		// Gather Opaque renderables
		auto& gather = composer.BeginPipelineStage();
		gather.Enqueue([&]() {
			const auto& registry = Editor::GetActiveScene().GetRegistry();
			const auto& view     = registry.view<MeshRendererComponent>();
			for (auto entityId : view) {
				const Entity entity(entityId, Editor::GetActiveScene());
				const auto transform      = entity.GetGlobalTransform();
				const auto& cMeshRenderer = entity.GetComponent<MeshRendererComponent>();
				const auto& meshMeta      = AssetManager::GetAssetMetadata(cMeshRenderer.MeshAsset);
				if (!meshMeta.IsValid()) { continue; }

				IntrusivePtr<Mesh> mesh = AssetManager::GetAsset<Mesh>(cMeshRenderer.MeshAsset, true);
				if (!mesh || !mesh->PositionBuffer) { continue; }

				for (uint32_t i = 0; i < mesh->Submeshes.size(); ++i) {
					IntrusivePtr<Material> material;
					if (cMeshRenderer.MaterialAssets.size() > i) {
						material = AssetManager::GetAsset<Material>(cMeshRenderer.MaterialAssets[i]);
					}

					_opaqueVisible.push_back(RenderableInfo{MakeHandle<StaticMesh>(mesh, i, material), transform});
				}
			}
		});

		if (_flags & SceneRendererFlagBits::ForwardZPrePass) {
			// Push Opaque renderables
			auto& push = composer.BeginPipelineStage();
			push.Enqueue([&]() {
				_depthQueue.PushDepthRenderables(_context, _opaqueVisible);
				_depthQueue.Sort();
			});
		}

		if (_flags & SceneRendererFlagBits::ForwardOpaque) {
			// Push Opaque renderables
			auto& push = composer.BeginPipelineStage();
			push.Enqueue([&]() {
				_opaqueQueue.PushRenderables(_context, _opaqueVisible);
				_opaqueQueue.Sort();
			});
		}
	}
}
}  // namespace Luna
