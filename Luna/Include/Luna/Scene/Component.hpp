#pragma once

#include <Luna/Utility/Serialization.hpp>

namespace Luna {
struct Component {
	virtual bool Deserialize(const nlohmann::json& data) = 0;
	virtual void Serialize(nlohmann::json& data) const   = 0;
};
}  // namespace Luna
