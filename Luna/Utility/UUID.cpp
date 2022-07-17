#include "UUID.hpp"

namespace Luna {
static std::random_device RandomDevice;
static std::mt19937_64 Generator(RandomDevice());
static std::uniform_int_distribution<uint64_t> Distribution;

UUID::UUID() : _uuid(Distribution(Generator)) {}

UUID::UUID(uint64_t uuid) : _uuid(uuid) {}

UUID::UUID(const UUID& other) : _uuid(other._uuid) {}
}  // namespace Luna
