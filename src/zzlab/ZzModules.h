#ifndef __ZZ_MODULES_H__
#define __ZZ_MODULES_H__

#include <vector>
#include <cstddef>

struct ZzModule {
	const char* name;
	void (*_init_)();
	void (*_uninit_)();

	ZzModule(const char* name, void (*_init_)(), void (*_uninit_)());
};

struct ZzModules {
	std::vector<ZzModule> modules;

	void init(const ZzModule& module);
	void uninit();
};

#define ZZ_MODULES_INIT() ZzModules* __modules = NULL

#define ZZ_MODULES_UNINIT() \
	__modules->uninit(); \
	delete __modules; \
	__modules = NULL

#define ZZ_MODULE_DECL(name) \
namespace name { \
	extern void _init_(); \
	extern void _uninit_(); \
}

#define ZZ_MODULE_INIT(name) \
	if(! __modules) __modules = new ZzModules; \
	__modules->init(ZzModule(#name, name::_init_, name::_uninit_))

#endif // __ZZ_MODULES_H__
