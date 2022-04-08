#include <Luna/Luna.hpp>
#include <iostream>

int main(int argc, const char** argv) {
	try {
		Luna::Engine engine;

		return engine.Run();
	} catch (const std::exception& e) {
		std::cerr << "Luna: Fatal engine exception!" << std::endl;
		std::cerr << e.what() << std::endl;

		return 1;
	}

	return 0;
}
