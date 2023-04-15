#include <Luna/Luna.hpp>

#ifdef _WIN32
#	define NOMINMAX
#	define WIN32_LEAN_AND_MEAN
#	include <Windows.h>
#	include <DbgHelp.h>
#	include <WinUser.h>
#	include <stdio.h>

extern "C" {
__declspec(dllexport) DWORD NvOptimusEnablement                = 0x00000001;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}

static LONG UnhandledExceptionHandler(LPEXCEPTION_POINTERS exceptions) {
	const BOOL haveSymbols = ::SymInitialize(::GetCurrentProcess(), NULL, TRUE);
	const auto record      = exceptions->ExceptionRecord;

	fprintf(stderr, "[Luna] =================================\n");
	fprintf(stderr, "[Luna] === FATAL UNHANDLED EXCEPTION ===\n");
	fprintf(stderr, "[Luna] =================================\n");
	fprintf(stderr, "[Luna] Exception Code: 0x%lX\n", record->ExceptionCode);

	fprintf(stderr, "[Luna] Exception Occurred At: 0x%zX", reinterpret_cast<uintptr_t>(record->ExceptionAddress));
	if (haveSymbols) {
		char symbolName[sizeof(SYMBOL_INFO) + sizeof(CHAR) * 1024];
		PSYMBOL_INFO symbolInfo  = reinterpret_cast<PSYMBOL_INFO>(&symbolName);
		symbolInfo->SizeOfStruct = sizeof(SYMBOL_INFO);
		symbolInfo->MaxNameLen   = 1024;

		if (::SymFromAddr(::GetCurrentProcess(), reinterpret_cast<uintptr_t>(record->ExceptionAddress), 0, symbolInfo)) {
			fprintf(stderr, " (%s)\n", symbolInfo->Name);
		} else {
			fprintf(stderr, " (Failed to retreive symbol name)\n");
		}
	} else {
		fprintf(stderr, " (Symbols unavailable)\n");
	}

	if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION) {
		fprintf(stderr, "[Luna] - Access Violation while ");
		if (record->ExceptionInformation[0] == 1) {
			fprintf(stderr, "writing");
		} else if (record->ExceptionInformation[0] == 0) {
			fprintf(stderr, "reading");
		} else if (record->ExceptionInformation[0] == 8) {
			fprintf(stderr, "executing");
		} else {
			fprintf(stderr, "accessing");
		}
		fprintf(stderr, " memory at 0x%zX\n", record->ExceptionInformation[1]);
	}

	fflush(stderr);

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

	try {
		if (!Engine::Initialize(options)) { return -1; }
		const int ret = Engine::Run();
		Engine::Shutdown();

		return ret;
	} catch (const std::exception& e) {
		fprintf(stderr, "[Luna] =================================\n");
		fprintf(stderr, "[Luna] === FATAL UNHANDLED EXCEPTION ===\n");
		fprintf(stderr, "[Luna] =================================\n");
		fprintf(stderr, "[Luna] Exception Message: %s\n", e.what());

		return -1;
	}
}
