#include <Luna/Core/Engine.hpp>

#include "CrashHandler.hpp"

#ifdef _WIN32
#	define NOMINMAX
#	define WIN32_LEAN_AND_MEAN
#	include <Windows.h>

extern "C" {
__declspec(dllexport) DWORD NvOptimusEnablement                = 1;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

int main(int argc, const char** argv) {
	if (!CrashHandler::Initialize()) { return -1; }

	int returnValue = 0;
	try {
		if (!Luna::Engine::Initialize()) { return -1; }
		returnValue = Luna::Engine::Run();
		Luna::Engine::Shutdown();
	} catch (const std::exception& e) {
		fprintf(stderr, "[Luna] =================================\n");
		fprintf(stderr, "[Luna] === FATAL UNHANDLED EXCEPTION ===\n");
		fprintf(stderr, "[Luna] =================================\n");
		fprintf(stderr, "[Luna] Exception Message: %s\n", e.what());

		fflush(stderr);

		returnValue = -1;
	}

	CrashHandler::Shutdown();

	return returnValue;
}
