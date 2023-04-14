#include <Luna/Core/CommandLine.hpp>
#include <string>
#include <unordered_map>

namespace Luna {
static struct CommandLineState {
	std::string Argv0;
	std::unordered_map<std::string, std::string> Arguments;
} Cmd;

const char* CommandLine::GetArgument(const char* name) {
	const auto it = Cmd.Arguments.find(name);
	if (it == Cmd.Arguments.end()) { return nullptr; }

	return it->second.c_str();
}

bool CommandLine::HasArgument(const char* name) {
	return Cmd.Arguments.contains(name);
}

void CommandLine::AddArgument(const char* name, const char* value) {
	Cmd.Arguments[name] = value;
}

void CommandLine::Parse(int argc, const char** argv) {
	Cmd.Argv0 = argv[0];

	const auto IsSwitch = [](const char* arg) {
		const auto len = strlen(arg);
		if (len < 3) { return false; }

		return arg[0] == '-' && arg[1] == '-';
	};

	for (int i = 1; i < argc; ++i) {
		const auto len = strlen(argv[i]);
		if (len < 3) { continue; }
		if (!IsSwitch(argv[i])) { continue; }

		if (i < argc - 1 && !IsSwitch(argv[i + 1])) {
			AddArgument(argv[i] + 2, argv[i + 1]);
			i++;
		} else {
			AddArgument(argv[i] + 2, "");
		}
	}
}
}  // namespace Luna
