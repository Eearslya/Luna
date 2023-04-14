#include <Luna/Luna.hpp>

#ifdef _WIN32
#	define NOMINMAX
#	define WIN32_LEAN_AND_MEAN
#	include <Windows.h>
#	include <DbgHelp.h>
#	include <WinUser.h>

extern "C" {
__declspec(dllexport) DWORD NvOptimusEnablement                = 0x00000001;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}

static LONG UnhandledExceptionHandler(LPEXCEPTION_POINTERS exceptions) {
	const BOOL haveSymbols = ::SymInitialize(::GetCurrentProcess(), NULL, TRUE);

	return EXCEPTION_CONTINUE_SEARCH;
}

#endif

using namespace Luna;

int main(int argc, const char** argv) {
#ifdef _WIN32
	::SetUnhandledExceptionFilter(UnhandledExceptionHandler);
#endif

	CommandLine::Parse(argc, argv);

	EngineOptions options = {};

	if (!Engine::Initialize(options)) { return -1; }
	const int ret = Engine::Run();
	Engine::Shutdown();

	return ret;
}
