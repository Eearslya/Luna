#include <Luna/Luna.hpp>

#ifdef _WIN32
#	define NOMINMAX
#	define WIN32_LEAN_AND_MEAN
#	include <Windows.h>
#	include <DbgHelp.h>
#	include <WinUser.h>
#	include <stdio.h>

static HANDLE SymHandle = INVALID_HANDLE_VALUE;

extern "C" {
__declspec(dllexport) DWORD NvOptimusEnablement                = 0x00000001;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}

static LONG UnhandledExceptionHandler(LPEXCEPTION_POINTERS exceptions) {
	const auto record = exceptions->ExceptionRecord;

	struct Symbol {
		const void* Address     = nullptr;
		const char* Name        = nullptr;
		size_t NameLength       = 0;
		const char* SourceFile  = nullptr;
		size_t SourceFileLength = 0;
		uint32_t SourceLine     = 0;
	};

	const auto GetSymbol = [](const void* ptr) -> Symbol {
		const auto addr = reinterpret_cast<uintptr_t>(ptr);

		Symbol symbol;
		symbol.Address = ptr;

		char symbolName[sizeof(SYMBOL_INFO) + sizeof(CHAR) * 1024];
		PSYMBOL_INFO symbolInfo  = reinterpret_cast<PSYMBOL_INFO>(&symbolName);
		symbolInfo->SizeOfStruct = sizeof(SYMBOL_INFO);
		symbolInfo->MaxNameLen   = 1024;

		if (::SymFromAddr(SymHandle, addr, 0, symbolInfo)) {
			symbol.Name       = symbolInfo->Name;
			symbol.NameLength = symbolInfo->NameLen;
		}

		IMAGEHLP_LINE64 line;
		DWORD displacement = 0;
		line.SizeOfStruct  = sizeof(line);
		if (::SymGetLineFromAddr64(SymHandle, addr, &displacement, &line)) {
			symbol.SourceFile       = line.FileName;
			symbol.SourceFileLength = strlen(line.FileName);
			symbol.SourceLine       = line.LineNumber;
		}

		return symbol;
	};
	const auto PrintSymbol = [](const Symbol& sym) {
		fprintf(stderr, "0x%016zX", reinterpret_cast<uintptr_t>(sym.Address));

		if (sym.Name) {
			fprintf(stderr, " (%s)", sym.Name);
		} else {
			fprintf(stderr, " (Symbol unavailable)");
		}

		if (sym.SourceFile) { fprintf(stderr, " - (%s:%u)", sym.SourceFile, sym.SourceLine); }
	};
	const auto PrintSymbolAligned = [](const Symbol& sym, uint32_t maxSymLength) {
		fprintf(stderr, "0x%016zX (", reinterpret_cast<uintptr_t>(sym.Address));

		int writtenLen = 0;
		if (sym.Name) {
			writtenLen = fprintf(stderr, "%s", sym.Name);
		} else {
			writtenLen = fprintf(stderr, "Symbol unavailable");
		}
		fprintf(stderr, ")");

		if (sym.SourceFile) {
			fprintf(stderr, "%*c(%s:%u)", (maxSymLength - writtenLen) + 1, ' ', sym.SourceFile, sym.SourceLine);
		}
	};

	const Symbol exceptSymbol = GetSymbol(record->ExceptionAddress);

	fprintf(stderr, "[Luna] =================================\n");
	fprintf(stderr, "[Luna] === FATAL UNHANDLED EXCEPTION ===\n");
	fprintf(stderr, "[Luna] =================================\n");
	fprintf(stderr, "[Luna] Exception Code: 0x%lX\n", record->ExceptionCode);

	fprintf(stderr, "[Luna] Exception Occurred At: ");
	PrintSymbol(exceptSymbol);
	fprintf(stderr, "\n");

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
		fprintf(stderr, " memory at 0x%016zX\n", record->ExceptionInformation[1]);
	}

	constexpr static int BtMax = 32;
	void* backtrace[BtMax]     = {nullptr};
	USHORT btSize              = ::CaptureStackBackTrace(0, BtMax, backtrace, NULL);

	// The backtrace we just captured will include all of the exception handling code, including this function.
	// So we first walk the backtrace to see if it includes our exception address.
	// If it does, we offset the backtrace so that it starts at the function that caused the exception instead.
	for (int i = 0; i < btSize; ++i) {
		if (backtrace[i] == record->ExceptionAddress) {
			btSize = ::CaptureStackBackTrace(i, BtMax, backtrace, NULL);
			break;
		}
	}

	// Determine which symbol name in our stacktrace is the longest, so we can nicely space out the symbol name from
	// source location.
	uint32_t maxSymLength = 0;
	for (int i = 0; i < btSize; ++i) {
		const auto sym = GetSymbol(backtrace[i]);
		if (sym.NameLength > maxSymLength) { maxSymLength = sym.NameLength; }
	}

	const char* spacer = "";
	if (btSize > 9) { spacer = " "; }
	if (btSize > 99) { spacer = "  "; }
	if (btSize > 0) {
		fprintf(stderr, "[Luna]\n");
		fprintf(stderr, "[Luna] Backtrace (up to %d frames):\n", BtMax);
		for (int i = 0; i < btSize; ++i) {
			int indent = 0;
			if (i > 9) { indent = 1; }
			if (i > 99) { indent = 2; }

			fprintf(stderr, "[Luna] - %d:%s ", i, spacer + indent);
			PrintSymbolAligned(GetSymbol(backtrace[i]), maxSymLength);
			fprintf(stderr, "\n");
		}
	}

	fflush(stderr);

	return EXCEPTION_CONTINUE_SEARCH;
}
#endif

using namespace Luna;

int main(int argc, const char** argv) {
#ifdef _WIN32
	SymHandle                  = ::GetCurrentProcess();
	const DWORD currentOptions = ::SymSetOptions(0);
	::SymSetOptions(currentOptions | SYMOPT_LOAD_LINES);
	::SymInitialize(SymHandle, NULL, TRUE);
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

#ifdef _WIN32
	::SymCleanup(SymHandle);
#endif
}
