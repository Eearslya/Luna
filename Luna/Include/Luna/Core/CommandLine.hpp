#pragma once

namespace Luna {
class CommandLine final {
 public:
	static const char* GetArgument(const char* name);
	static bool HasArgument(const char* name);

	static void AddArgument(const char* name, const char* value);
	static void Parse(int argc, const char** argv);
};
}  // namespace Luna
