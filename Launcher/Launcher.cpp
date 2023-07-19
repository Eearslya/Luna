#include <Luna/Core/Engine.hpp>

#ifdef _WIN32
#	define NOMINMAX
#	define WIN32_LEAN_AND_MEAN
#	include <Windows.h>
#	include <DbgHelp.h>
#	include <WinUser.h>
#	include <stdio.h>

static HANDLE SymHandle = INVALID_HANDLE_VALUE;

extern "C" {
__declspec(dllexport) DWORD NvOptimusEnablement                = 1;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}

static LONG UnhandledExceptionHandler(LPEXCEPTION_POINTERS exceptions) {
	constexpr static int MaxBacktraceFrames  = 32;
	constexpr static int MaxFileNameLength   = MAX_PATH;
	constexpr static int MaxSymbolNameLength = 1024;

	const auto record = exceptions->ExceptionRecord;

	struct Symbol {
		const void* Address = nullptr;
		char Name[MaxSymbolNameLength];
		size_t NameLength = 0;
		char SourceFile[MaxFileNameLength];
		size_t SourceFileLength = 0;
		unsigned int SourceLine = 0;
	};

	const auto GetSymbol = [](const void* ptr) -> Symbol {
		const auto addr = reinterpret_cast<uintptr_t>(ptr);

		Symbol symbol;
		symbol.Address = ptr;

		char symbolName[sizeof(SYMBOL_INFO) + sizeof(CHAR) * MaxSymbolNameLength];
		PSYMBOL_INFO symbolInfo  = reinterpret_cast<PSYMBOL_INFO>(&symbolName);
		symbolInfo->SizeOfStruct = sizeof(SYMBOL_INFO);
		symbolInfo->MaxNameLen   = MaxSymbolNameLength;

		if (::SymFromAddr(SymHandle, addr, 0, symbolInfo)) {
			strncpy_s(symbol.Name, symbolInfo->Name, symbolInfo->NameLen);
			symbol.NameLength = symbolInfo->NameLen;
		}

		IMAGEHLP_LINE64 line;
		DWORD displacement = 0;
		line.SizeOfStruct  = sizeof(line);
		if (::SymGetLineFromAddr64(SymHandle, addr, &displacement, &line)) {
			const auto fileNameLength = strlen(line.FileName);
			strncpy_s(symbol.SourceFile, line.FileName, fileNameLength);
			symbol.SourceFileLength = fileNameLength;
			symbol.SourceLine       = line.LineNumber;
		}

		return symbol;
	};
	const auto PrintSymbolAligned = [](const Symbol& sym, unsigned int maxSymLength) {
		fprintf(stderr, "0x%012zX (", reinterpret_cast<uintptr_t>(sym.Address));

		int writtenLen = 0;
		if (sym.NameLength > 0) {
			writtenLen = fprintf(stderr, "%s", sym.Name);
		} else {
			writtenLen = fprintf(stderr, "Symbol unavailable");
		}
		fprintf(stderr, ")");

		if (sym.SourceFileLength > 0) {
			fprintf(stderr, "%*c(%s:%u)", (maxSymLength - writtenLen) + 1, ' ', sym.SourceFile, sym.SourceLine);
		}
	};
	const auto PrintSymbol = [&](const Symbol& sym) { PrintSymbolAligned(sym, sym.NameLength); };

	const Symbol exceptSymbol = GetSymbol(record->ExceptionAddress);

	fprintf(stderr, "[Luna] =================================\n");
	fprintf(stderr, "[Luna] === FATAL UNHANDLED EXCEPTION ===\n");
	fprintf(stderr, "[Luna] =================================\n");
	fprintf(stderr, "[Luna] Exception Code: 0x%lX ", record->ExceptionCode);
	switch (record->ExceptionCode) {
		case EXCEPTION_ACCESS_VIOLATION:
			fprintf(stderr, "(Access Violation)");
			break;
		case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
			fprintf(stderr, "(Array Bounds Exceeded)");
			break;
		case EXCEPTION_BREAKPOINT:
			fprintf(stderr, "(Breakpoint)");
			break;
		case EXCEPTION_DATATYPE_MISALIGNMENT:
			fprintf(stderr, "(Data Type Misalignment)");
			break;
		case EXCEPTION_FLT_DENORMAL_OPERAND:
			fprintf(stderr, "(Denormal Floating-Point Operand)");
			break;
		case EXCEPTION_FLT_DIVIDE_BY_ZERO:
			fprintf(stderr, "(Floating-Point Divide By Zero)");
			break;
		case EXCEPTION_FLT_INEXACT_RESULT:
			fprintf(stderr, "(Inexact Floating-Point Result)");
			break;
		case EXCEPTION_FLT_INVALID_OPERATION:
			fprintf(stderr, "(Invalid Floating-Point Operation)");
			break;
		case EXCEPTION_FLT_OVERFLOW:
			fprintf(stderr, "(Floating-Point Overflow)");
			break;
		case EXCEPTION_FLT_STACK_CHECK:
			fprintf(stderr, "(Floating-Point Stack Overflow)");
			break;
		case EXCEPTION_FLT_UNDERFLOW:
			fprintf(stderr, "(Floating-Point Underflow)");
			break;
		case EXCEPTION_ILLEGAL_INSTRUCTION:
			fprintf(stderr, "(Illegal Instruction)");
			break;
		case EXCEPTION_IN_PAGE_ERROR:
			fprintf(stderr, "(Page Error)");
			break;
		case EXCEPTION_INT_DIVIDE_BY_ZERO:
			fprintf(stderr, "(Integer Divide By Zero)");
			break;
		case EXCEPTION_INT_OVERFLOW:
			fprintf(stderr, "(Integer Overflow)");
			break;
		case EXCEPTION_INVALID_DISPOSITION:
			fprintf(stderr, "(Invalid Disposition)");
			break;
		case EXCEPTION_NONCONTINUABLE_EXCEPTION:
			fprintf(stderr, "(Non-Continuable Exception)");
			break;
		case EXCEPTION_PRIV_INSTRUCTION:
			fprintf(stderr, "(Privileged Instruction)");
			break;
		case EXCEPTION_SINGLE_STEP:
			fprintf(stderr, "(Single Step)");
			break;
		case EXCEPTION_STACK_OVERFLOW:
			fprintf(stderr, "(Stack Overflow)");
			break;
		default:
			fprintf(stderr, "(Unknown)");
			break;
	}
	fprintf(stderr, "\n");

	fprintf(stderr, "[Luna] Exception Occurred At: ");
	PrintSymbol(exceptSymbol);
	fprintf(stderr, "\n");

	if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2) {
		const auto accessType      = record->ExceptionInformation[0];
		const auto accessedAddress = record->ExceptionInformation[1];

		fprintf(stderr, "[Luna] - Access Violation while ");
		if (accessType == 0) {
			fprintf(stderr, "reading");
		} else if (accessType == 1) {
			fprintf(stderr, "writing");
		} else if (accessType == 8) {
			fprintf(stderr, "executing");
		} else {
			fprintf(stderr, "accessing");
		}
		fprintf(stderr, " memory at 0x%012zX\n", accessedAddress);
	}

	void* backtrace[MaxBacktraceFrames] = {nullptr};
	Symbol backtraceSymbols[MaxBacktraceFrames];
	USHORT btSize = ::CaptureStackBackTrace(0, MaxBacktraceFrames, backtrace, NULL);

	// The backtrace we just captured will include all of the exception handling code, including this function.
	// So we first walk the backtrace to see if it includes our exception address.
	// If it does, we offset the backtrace so that it starts at the function that caused the exception instead.
	for (int i = 0; i < btSize; ++i) {
		if (backtrace[i] == record->ExceptionAddress) {
			btSize = ::CaptureStackBackTrace(i, MaxBacktraceFrames, backtrace, NULL);
			break;
		}
	}

	// Determine which symbol name in our stacktrace is the longest, so we can nicely space out the symbol name from
	// source location.
	unsigned int maxSymLength = 0;
	for (int i = 0; i < btSize; ++i) {
		backtraceSymbols[i] = GetSymbol(backtrace[i]);
		if (backtraceSymbols[i].NameLength > maxSymLength) { maxSymLength = backtraceSymbols[i].NameLength; }
	}

	fprintf(stderr, "[Luna]\n");
	const char* spacer = "";
	if (btSize > 9) { spacer = " "; }
	if (btSize > 99) { spacer = "  "; }
	if (btSize > 0) {
		fprintf(stderr, "[Luna] Backtrace (up to %d frames):\n", MaxBacktraceFrames);
		for (int i = 0; i < btSize; ++i) {
			int indent = 0;
			if (i > 9) { indent = 1; }
			if (i > 99) { indent = 2; }

			fprintf(stderr, "[Luna] - %d:%s ", i, spacer + indent);
			PrintSymbolAligned(backtraceSymbols[i], maxSymLength);
			fprintf(stderr, "\n");
		}
	} else {
		fprintf(stderr, "[Luna] No backtrace available.\n");
	}

	fflush(stderr);

	return EXCEPTION_CONTINUE_SEARCH;
}
#endif

int main(int argc, const char** argv) {
#ifdef _WIN32
	SymHandle                  = ::GetCurrentProcess();
	const DWORD currentOptions = ::SymSetOptions(0);
	::SymSetOptions(currentOptions | SYMOPT_LOAD_LINES);
	::SymInitialize(SymHandle, NULL, TRUE);
	::SetUnhandledExceptionFilter(UnhandledExceptionHandler);
#endif

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

#ifdef _WIN32
	::SymCleanup(SymHandle);
#endif

	return returnValue;
}
