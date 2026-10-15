#pragma once

#include <functional>
#include <memory>
#include <string>

class component_interface
{
public:
	virtual ~component_interface() = default;
	virtual void post_load() {}
	virtual void pre_destroy() {}
};

namespace tb::components
{
	using factory_t = std::function<std::unique_ptr<component_interface>()>;
	// lower priority runs first
	void register_factory(const char* name, int priority, factory_t factory);
}

#define TB_REGISTER_COMPONENT(type, priority)                                        \
	namespace                                                                         \
	{                                                                                 \
		const bool registered_component = [] {                                        \
			tb::components::register_factory(#type, priority, [] {                    \
				return std::unique_ptr<component_interface>(new type());              \
			});                                                                       \
			return true;                                                              \
		}();                                                                          \
	}
