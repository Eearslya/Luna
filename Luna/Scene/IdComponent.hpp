#pragma once

#include "Utility/UUID.hpp"

namespace Luna {
struct IdComponent {
	IdComponent()                   = default;
	IdComponent(const IdComponent&) = default;
	IdComponent(const UUID& uuid) : Id(uuid) {}

	UUID Id;
};
}  // namespace Luna
