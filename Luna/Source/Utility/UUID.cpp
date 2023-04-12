#include <Luna/Utility/UUID.hpp>
#include <random>

namespace Luna {
static std::random_device RandomDevice;
static std::mt19937_64 Generator(RandomDevice());
static std::uniform_int_distribution<uint64_t> Distribution;

UUID::UUID() : _uuid(Distribution(Generator)) {}

UUID::UUID(uint64_t uuid) : _uuid(uuid) {}

UUID::UUID(const UUID& other) {
	*this = other;
}

UUID::UUID(UUID&& other) noexcept {
	*this = std::move(other);
}

UUID& UUID::operator=(const UUID& other) {
	_uuid = other._uuid;

	return *this;
}

UUID& UUID::operator=(UUID&& other) noexcept {
	_uuid       = other._uuid;
	other._uuid = 0;

	return *this;
}

bool UUID::operator==(const UUID& other) const {
	return _uuid == other._uuid;
}

bool UUID::operator!=(const UUID& other) const {
	return _uuid != other._uuid;
}
}  // namespace Luna
