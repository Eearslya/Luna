#pragma once

#include <Luna/Application/Application.hpp>
#include <Luna/Renderer/RenderGraph.hpp>
#include <memory>

class Editor : public Luna::Application {
 public:
	virtual glm::uvec2 GetDefaultSize() const override;
	virtual std::string GetName() const override;

	virtual void Start() override;
	virtual void Render() override;
	virtual void Stop() override;

 protected:
	virtual void OnSwapchainChanged(const Luna::Vulkan::SwapchainConfiguration& config) override;

 private:
	void BakeRenderGraph();

	std::unique_ptr<Luna::RenderGraph> _renderGraph;
	Luna::Vulkan::SwapchainConfiguration _swapchainConfig;
	bool _swapchainDirty = true;
};
