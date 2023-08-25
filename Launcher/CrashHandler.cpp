#include "CrashHandler.hpp"

#ifdef _WIN32
#	define NOMINMAX
#	define WIN32_LEAN_AND_MEAN
#	include <Windows.h>
#	include <cstdlib>
#	include <psapi.h>
#	include <DbgHelp.h>
#	include <WinUser.h>
#	include <tlhelp32.h>
#	include <stdio.h>

namespace CrashHandler {
constexpr static int MaxBacktraceLength  = 32;
constexpr static int MaxModules          = 128;
constexpr static int MaxNameLength       = 128;
constexpr static int MaxPathLength       = 1024;
constexpr static int MaxSymbolLength     = 512;
constexpr static int MaxSearchPathLength = 4096;

struct SymbolModule {
	void* ModuleBase               = nullptr;
	DWORD ModuleSize               = 0;
	char ModuleName[MaxNameLength] = {0};
	char ModulePath[MaxPathLength] = {0};
};

struct Symbol {
	void* Address                = nullptr;
	void* ModuleBase             = nullptr;
	int NameLength               = 0;
	char Name[MaxSymbolLength]   = {0};
	char FilePath[MaxPathLength] = {0};
	int LineNumber               = 0;
};

Symbol Backtrace[MaxBacktraceLength]       = {};
int BacktraceLength                        = 0;
bool CrashHandlerReady                     = false;
HANDLE CurrentProcess                      = NULL;
DWORD CurrentProcessID                     = 0;
HMODULE ModDbgHelp                         = NULL;
int SymbolModuleCount                      = 0;
SymbolModule SymbolModules[MaxModules]     = {};
char SymbolSearchPath[MaxSearchPathLength] = {0};

using pfnSymInitialize            = BOOL(__stdcall*)(HANDLE, PCSTR, BOOL);
using pfnStackWalkEx              = BOOL(__stdcall*)(DWORD,
                                        HANDLE,
                                        HANDLE,
                                        LPSTACKFRAME_EX,
                                        PVOID,
                                        PREAD_PROCESS_MEMORY_ROUTINE64,
                                        PFUNCTION_TABLE_ACCESS_ROUTINE64,
                                        PGET_MODULE_BASE_ROUTINE64,
                                        PTRANSLATE_ADDRESS_ROUTINE64,
                                        DWORD);
using pfnSymFunctionTableAccess64 = PVOID(__stdcall*)(HANDLE, DWORD64);
using pfnSymGetModuleBase64       = DWORD64(__stdcall*)(HANDLE, DWORD64);
using pfnSymFromAddr              = BOOL(__stdcall*)(HANDLE, DWORD64, PDWORD64, PSYMBOL_INFO);
using pfnSymGetLineFromAddr64     = BOOL(__stdcall*)(HANDLE, DWORD64, PDWORD, PIMAGEHLP_LINE64);
using pfnSymGetOptions            = DWORD(__stdcall*)();
using pfnSymSetOptions            = DWORD(__stdcall*)(DWORD);
using pfnSymLoadModuleEx = DWORD64(__stdcall*)(HANDLE, HANDLE, PCSTR, PCSTR, DWORD64, DWORD, PMODLOAD_DATA, DWORD);

pfnSymInitialize pSymInitialize                       = nullptr;
pfnStackWalkEx pStackWalkEx                           = nullptr;
pfnSymFunctionTableAccess64 pSymFunctionTableAccess64 = nullptr;
pfnSymGetModuleBase64 pSymGetModuleBase64             = nullptr;
pfnSymFromAddr pSymFromAddr                           = nullptr;
pfnSymGetLineFromAddr64 pSymGetLineFromAddr64         = nullptr;
pfnSymGetOptions pSymGetOptions                       = nullptr;
pfnSymSetOptions pSymSetOptions                       = nullptr;
pfnSymLoadModuleEx pSymLoadModuleEx                   = nullptr;

static void LoadModules() {
	using pfnCreateToolhelp32Snapshot = HANDLE(__stdcall*)(DWORD, DWORD);
	using pfnModule32First            = BOOL(__stdcall*)(HANDLE, LPMODULEENTRY32);
	using pfnModule32Next             = BOOL(__stdcall*)(HANDLE, LPMODULEENTRY32);

	HMODULE toolhelp                                      = NULL;
	pfnCreateToolhelp32Snapshot pCreateToolhelp32Snapshot = nullptr;
	pfnModule32First pModule32First                       = nullptr;
	pfnModule32Next pModule32Next                         = nullptr;

	constexpr static const char* dllNames[] = {"kernel32.dll", "tlhelp32.dll"};
	for (size_t i = 0; i < 2; ++i) {
		toolhelp = ::LoadLibraryA(dllNames[i]);
		if (toolhelp == NULL) { continue; }

		pCreateToolhelp32Snapshot =
			reinterpret_cast<pfnCreateToolhelp32Snapshot>(::GetProcAddress(toolhelp, "CreateToolhelp32Snapshot"));
		pModule32First = reinterpret_cast<pfnModule32First>(::GetProcAddress(toolhelp, "Module32First"));
		pModule32Next  = reinterpret_cast<pfnModule32Next>(::GetProcAddress(toolhelp, "Module32Next"));

		if (pCreateToolhelp32Snapshot != NULL && pModule32First != NULL && pModule32Next != NULL) { break; }

		::FreeLibrary(toolhelp);
		toolhelp = NULL;
	}
	if (toolhelp == NULL) { return; }

	HANDLE snapshot = pCreateToolhelp32Snapshot(TH32CS_SNAPMODULE, CurrentProcessID);
	if (snapshot == INVALID_HANDLE_VALUE) {
		::FreeLibrary(toolhelp);
		return;
	}

	MODULEENTRY32 moduleEntry = {};
	moduleEntry.dwSize        = sizeof(moduleEntry);

	SymbolModuleCount = 0;

	bool modulesRemaining = !!pModule32First(snapshot, &moduleEntry);
	while (modulesRemaining) {
		SymbolModule& mod = SymbolModules[SymbolModuleCount];
		mod.ModuleBase    = moduleEntry.modBaseAddr;
		mod.ModuleSize    = moduleEntry.modBaseSize;
		::strncpy_s(mod.ModuleName, moduleEntry.szModule, MaxNameLength);
		::strncpy_s(mod.ModulePath, moduleEntry.szExePath, MaxPathLength);

		/*
		pSymLoadModuleEx(CurrentProcess,
		                 NULL,
		                 moduleEntry.szExePath,
		                 moduleEntry.szModule,
		                 reinterpret_cast<DWORD64>(moduleEntry.modBaseAddr),
		                 moduleEntry.dwSize,
		                 NULL,
		                 0);
		*/

		++SymbolModuleCount;
		modulesRemaining = !!pModule32Next(snapshot, &moduleEntry);
	}

	::CloseHandle(snapshot);
	::FreeLibrary(toolhelp);
}

static LONG UnhandledExceptionHandler(LPEXCEPTION_POINTERS exceptionPointers) {
	if (!CrashHandlerReady || exceptionPointers == NULL || exceptionPointers->ContextRecord == NULL ||
	    exceptionPointers->ExceptionRecord == NULL) {
		return EXCEPTION_CONTINUE_SEARCH;
	}

	const auto& exceptionContext = *exceptionPointers->ContextRecord;
	const auto& exceptionRecord  = *exceptionPointers->ExceptionRecord;

	fprintf(stderr, "[Luna] =================================\n");
	fprintf(stderr, "[Luna] === FATAL UNHANDLED EXCEPTION ===\n");
	fprintf(stderr, "[Luna] =================================\n");

	DWORD options = pSymGetOptions();
	options |= SYMOPT_LOAD_LINES;
	pSymSetOptions(options);

	if (!pSymInitialize(CurrentProcess, SymbolSearchPath, TRUE)) { fprintf(stderr, "SymInitialize error\n"); }

	LoadModules();

	if (SymbolModuleCount > 0) {
		fprintf(stderr, "[Luna] Loaded modules (%d):\n", SymbolModuleCount);
		for (int i = 0; i < SymbolModuleCount; ++i) {
			fprintf(stderr,
			        "[Luna] - (0x%012zX - 0x%012zX) %s\n",
			        (unsigned long long) SymbolModules[i].ModuleBase,
			        ((unsigned long long) SymbolModules[i].ModuleBase) + SymbolModules[i].ModuleSize,
			        SymbolModules[i].ModulePath);
		}
		fprintf(stderr, "\n");
	} else {
		fprintf(stderr, "[Luna] Unable to enumerate loaded modules.\n\n");
	}

	fprintf(stderr, "[Luna] Exception Code: 0x%lX ", exceptionRecord.ExceptionCode);
	switch (exceptionRecord.ExceptionCode) {
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

	if (exceptionRecord.ExceptionCode == EXCEPTION_ACCESS_VIOLATION && exceptionRecord.NumberParameters >= 2) {
		const auto accessType      = exceptionRecord.ExceptionInformation[0];
		const auto accessedAddress = exceptionRecord.ExceptionInformation[1];

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
	fprintf(stderr, "\n");

	STACKFRAME_EX stackFrame    = {0};
	stackFrame.AddrPC.Offset    = exceptionContext.Rip;
	stackFrame.AddrPC.Mode      = AddrModeFlat;
	stackFrame.AddrFrame.Offset = exceptionContext.Rbp;
	stackFrame.AddrFrame.Mode   = AddrModeFlat;
	stackFrame.AddrStack.Offset = exceptionContext.Rsp;
	stackFrame.AddrStack.Mode   = AddrModeFlat;

	DWORD displacement = 0;

	constexpr int SymbolBufferSize = sizeof(SYMBOL_INFO) + (MaxSymbolLength * sizeof(TCHAR));
	char symbolBuffer[SymbolBufferSize];
	SYMBOL_INFO& symbolInfo = *reinterpret_cast<SYMBOL_INFO*>(symbolBuffer);
	symbolInfo.SizeOfStruct = sizeof(symbolInfo);
	symbolInfo.MaxNameLen   = MaxSymbolLength;

	IMAGEHLP_LINE64 lineInfo = {0};
	lineInfo.SizeOfStruct    = sizeof(lineInfo);

	BacktraceLength = 0;
	while (true) {
		if (pStackWalkEx(IMAGE_FILE_MACHINE_AMD64,
		                 CurrentProcess,
		                 ::GetCurrentThread(),
		                 &stackFrame,
		                 exceptionPointers->ContextRecord,
		                 NULL,
		                 pSymFunctionTableAccess64,
		                 pSymGetModuleBase64,
		                 NULL,
		                 SYM_STKWALK_DEFAULT) == TRUE) {
			Symbol& sym = Backtrace[BacktraceLength];
			sym.Address = reinterpret_cast<void*>(stackFrame.AddrPC.Offset);

			if (pSymFromAddr(CurrentProcess, stackFrame.AddrPC.Offset, NULL, &symbolInfo)) {
				sym.ModuleBase = reinterpret_cast<void*>(symbolInfo.ModBase);
				sym.NameLength = symbolInfo.NameLen;
				::strncpy_s(sym.Name, symbolInfo.Name, symbolInfo.NameLen);
			}

			if (pSymGetLineFromAddr64(CurrentProcess, stackFrame.AddrPC.Offset, &displacement, &lineInfo) == TRUE) {
				::strncpy_s(sym.FilePath, lineInfo.FileName, strlen(lineInfo.FileName));
				sym.LineNumber = lineInfo.LineNumber;
			}

			++BacktraceLength;

			if (BacktraceLength >= MaxBacktraceLength) { break; }
		} else {
			break;
		}
	}

	if (BacktraceLength > 0) {
		const char* spacer = "";
		if (BacktraceLength > 9) { spacer = " "; }
		if (BacktraceLength > 99) { spacer = "  "; }

		fprintf(stderr, "[Luna] Backtrace (%d Frames):\n", BacktraceLength);
		for (int i = 0; i < BacktraceLength; ++i) {
			int indent = 0;
			if (i > 9) { indent = 1; }
			if (i > 99) { indent = 2; }

			const Symbol& sym = Backtrace[i];
			fprintf(stderr, "[Luna] - %d:%s ", i, spacer + indent);
			fprintf(stderr, "0x%012zX - ", (unsigned long long) sym.Address);

			if (sym.Name[0] == 0) {
				fprintf(stderr, "Name unavailable");
			} else {
				fprintf(stderr, "%s", sym.Name);
			}

			for (int j = 0; j < SymbolModuleCount; ++j) {
				const auto minAddress = reinterpret_cast<unsigned long long>(SymbolModules[j].ModuleBase);
				const auto maxAddress = minAddress + SymbolModules[j].ModuleSize;
				const auto address    = reinterpret_cast<unsigned long long>(sym.Address);

				if (sym.ModuleBase == SymbolModules[j].ModuleBase || (address >= minAddress && address < maxAddress)) {
					fprintf(stderr, "  <%s>", SymbolModules[j].ModuleName);
					break;
				}
			}

			fprintf(stderr, "\n");

			if (sym.FilePath[0] != 0) { fprintf(stderr, "[Luna]     %s   %s:%d\n", spacer, sym.FilePath, sym.LineNumber); }

			if (i < (BacktraceLength - 1)) { fprintf(stderr, "[Luna]\n"); }
		}
		fprintf(stderr, "\n");
	} else {
		fprintf(stderr, "[Luna] No backtrace available.\n\n");
	}

	fflush(stderr);

	return EXCEPTION_CONTINUE_SEARCH;
}

bool Initialize() noexcept {
	CurrentProcess   = ::GetCurrentProcess();
	CurrentProcessID = ::GetCurrentProcessId();

	ModDbgHelp = ::LoadLibraryA("dbghelp.dll");
	if (ModDbgHelp == NULL) {
		fprintf(stderr, "Failed to load dbghelp\n");

		return false;
	}

#	define DbgHelpFn(name)                                                       \
		p##name = reinterpret_cast<pfn##name>(::GetProcAddress(ModDbgHelp, #name)); \
		if (p##name == NULL) {                                                      \
			fprintf(stderr, "Failed to load " #name "\n");                            \
			return false;                                                             \
		}

	DbgHelpFn(SymInitialize);
	DbgHelpFn(StackWalkEx);
	DbgHelpFn(SymFunctionTableAccess64);
	DbgHelpFn(SymGetModuleBase64);
	DbgHelpFn(SymFromAddr);
	DbgHelpFn(SymGetLineFromAddr64);
	DbgHelpFn(SymGetOptions);
	DbgHelpFn(SymSetOptions);
	DbgHelpFn(SymLoadModuleEx);

#	undef DbgHelpFn

	// Initialize Symbol Search Path
	{
		constexpr static int MaxTempPathLength = 1024;
		char tempPath[1024];

		::memset(SymbolSearchPath, 0, sizeof(SymbolSearchPath));
		::memset(tempPath, 0, sizeof(tempPath));

		::strcat_s(SymbolSearchPath, MaxSearchPathLength, ".;");

		if (::GetCurrentDirectoryA(MaxTempPathLength, tempPath) > 0) {
			tempPath[MaxTempPathLength - 1] = 0;
			::strcat_s(SymbolSearchPath, MaxSearchPathLength, tempPath);
			::strcat_s(SymbolSearchPath, MaxSearchPathLength, ";");
		}

		if (::GetModuleFileNameA(NULL, tempPath, MaxTempPathLength) > 0) {
			tempPath[MaxTempPathLength - 1] = 0;

			for (char* p = (tempPath + strlen(tempPath) - 1); p >= tempPath; --p) {
				if ((*p == '\\') || (*p == '/') || (*p == ':')) {
					*p = 0;
					break;
				}
			}

			if (strlen(tempPath) > 0) {
				::strcat_s(SymbolSearchPath, MaxSearchPathLength, tempPath);
				::strcat_s(SymbolSearchPath, MaxSearchPathLength, ";");
			}
		}

		if (::GetEnvironmentVariableA("_NT_SYMBOL_PATH", tempPath, MaxTempPathLength) > 0) {
			tempPath[MaxTempPathLength - 1] = 0;
			::strcat_s(SymbolSearchPath, MaxSearchPathLength, tempPath);
			::strcat_s(SymbolSearchPath, MaxSearchPathLength, ";");
		}

		if (::GetEnvironmentVariableA("_NT_ALTERNATE_SYMBOL_PATH", tempPath, MaxTempPathLength) > 0) {
			tempPath[MaxTempPathLength - 1] = 0;
			::strcat_s(SymbolSearchPath, MaxSearchPathLength, tempPath);
			::strcat_s(SymbolSearchPath, MaxSearchPathLength, ";");
		}

		if (::GetEnvironmentVariableA("SYSTEMROOT", tempPath, MaxTempPathLength) > 0) {
			tempPath[MaxTempPathLength - 1] = 0;
			::strcat_s(SymbolSearchPath, MaxSearchPathLength, tempPath);
			::strcat_s(SymbolSearchPath, MaxSearchPathLength, ";");

			::strcat_s(tempPath, MaxTempPathLength, "\\System32");
			::strcat_s(SymbolSearchPath, MaxSearchPathLength, tempPath);
			::strcat_s(SymbolSearchPath, MaxSearchPathLength, ";");
		}

		if (::GetEnvironmentVariableA("SYSTEMDRIVE", tempPath, MaxTempPathLength) > 0) {
			tempPath[MaxTempPathLength - 1] = 0;
			::strcat_s(SymbolSearchPath, MaxSearchPathLength, "SRV*");
			::strcat_s(SymbolSearchPath, MaxSearchPathLength, tempPath);
			::strcat_s(SymbolSearchPath, MaxSearchPathLength, "\\WebSymbols*https://msdl.microsoft.com/download/symbols;");
		} else {
			::strcat_s(
				SymbolSearchPath, MaxSearchPathLength, "SRV*C:\\WebSymbols*https://msdl.microsoft.com/download/symbols;");
		}
	}

	::SetUnhandledExceptionFilter(UnhandledExceptionHandler);

	CrashHandlerReady = true;

	return true;
}

void Shutdown() noexcept {
	::SetUnhandledExceptionFilter(NULL);
	CrashHandlerReady = false;
	if (ModDbgHelp) { ::FreeLibrary(ModDbgHelp); }
}
}  // namespace CrashHandler
#endif
