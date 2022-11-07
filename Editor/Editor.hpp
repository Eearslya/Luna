#pragma once

#include <Luna/Application/Application.hpp>

class Editor : public Luna::Application {
 public:
	virtual glm::uvec2 GetDefaultSize() const override;
	virtual std::string GetName() const override;

	virtual void Render() override;
};
