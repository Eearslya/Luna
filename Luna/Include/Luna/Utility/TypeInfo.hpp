#pragma once

#include <typeindex>
#include <unordered_map>

namespace Luna {
using TypeID = std::size_t;

template <typename T>
class TypeInfo {
 public:
	TypeInfo() = delete;

	template <typename K, typename = std::enable_if_t<std::is_convertible_v<K*, T*>>>
	static TypeID GetTypeID() noexcept {
		const std::type_index typeIndex(typeid(K));
		const auto it = _typeMap.find(typeIndex);
		if (it != _typeMap.end()) { return it->second; }

		const auto id       = _nextTypeID++;
		_typeMap[typeIndex] = id;
		return id;
	}

 private:
	static TypeID _nextTypeID;
	static std::unordered_map<std::type_index, TypeID> _typeMap;
};

template <typename T>
TypeID TypeInfo<T>::_nextTypeID = 0;

template <typename T>
std::unordered_map<std::type_index, TypeID> TypeInfo<T>::_typeMap;
}  // namespace Luna
