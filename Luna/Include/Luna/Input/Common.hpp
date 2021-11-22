#pragma once

#include <Luna/Utility/EnumClass.hpp>

namespace Luna {
enum class InputAction : int32_t { Release = 0, Press = 1, Repeat = 2 };

enum class InputModBits : uint32_t { None = 0, Shift = 1 << 0, Control = 1 << 1, Alt = 1 << 2, Super = 1 << 3 };
EnableBitmask(InputMods, InputModBits);
}  // namespace Luna
