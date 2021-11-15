#pragma once

#include <Luna/Utility/NonCopyable.hpp>
#include <Luna/Utility/TypeInfo.hpp>
#include <functional>
#include <map>
#include <memory>
#include <vector>

namespace Luna {
template <typename Base>
class ModuleFactory {
 public:
	struct CreateInfo {
		std::function<std::unique_ptr<Base>()> Create;
		const char* Name;
		typename Base::Stage Stage;
		std::vector<TypeID> Dependencies;
	};
	using RegistryMapT = std::unordered_map<TypeID, CreateInfo>;

	template <typename... Dependencies>
	struct Depends {
		std::vector<TypeID> Get() const {
			std::vector<TypeID> dependencies;
			(dependencies.emplace_back(TypeInfo<Base>::template GetTypeID<Dependencies>()), ...);
			return dependencies;
		}
	};

	template <typename T>
	class Registrar : public Base {
	 public:
		static T* Get() {
			return _instance;
		}

	 protected:
		template <typename... Dependencies>
		static bool Register(const char* name, typename Base::Stage stage, Depends<Dependencies...>&& dependencies = {}) {
			ModuleFactory::Registry()[TypeInfo<Base>::template GetTypeID<T>()] = {[]() {
																																							_instance = new T();
																																							return std::unique_ptr<Base>(_instance);
																																						},
			                                                                      name,
			                                                                      stage,
			                                                                      dependencies.Get()};

			return true;
		}

		static inline T* _instance = nullptr;
	};

	virtual ~ModuleFactory() noexcept = default;

	static RegistryMapT& Registry() {
		static RegistryMapT registryMap;
		return registryMap;
	}

 private:
};

class Module : public ModuleFactory<Module>, NonCopyable {
 public:
	enum class Stage : uint8_t { Never, Always, Pre, Normal, Post, Render };
	using StageIndex = std::pair<Stage, std::size_t>;

	virtual ~Module() noexcept = default;

	virtual void Update() = 0;
};
template class TypeInfo<Module>;
}  // namespace Luna
