#include <iostream>
#include <stdexcept>

#include "Editor.hpp"

int main(int argc, const char** argv) {
	try {
		Editor editor;
		editor.Run();
	} catch (const std::exception& e) {
		std::cerr << "Uncaught exception when running Editor:\n\t" << e.what() << std::endl;
		return 1;
	}

	return 0;
}
