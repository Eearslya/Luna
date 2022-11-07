#include <Luna.hpp>

#include "Editor.hpp"

Luna::Application* Luna::CreateApplication(int argc, const char** argv) {
	return new Editor();
}
