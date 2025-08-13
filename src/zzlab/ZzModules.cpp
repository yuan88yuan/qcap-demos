#include "ZzModules.h"
#include "ZzLog.h"

ZZ_INIT_LOG("ZzModules");

ZzModule::ZzModule(const char* name, void (*_init_)(), void (*_uninit_)()) {
	this->name = name;
	this->_init_ = _init_;
	this->_uninit_ = _uninit_;
}

void ZzModules::init(const ZzModule& module) {
	if(__zz_log__::QCAP_LOG_LEVEL <= 3) { // LOGD
		printf("\033[1;34mModule %s init...\033[0m\n", module.name);
	}

	module._init_();

	modules.push_back(module);
}

void ZzModules::uninit() {
	for(std::vector<ZzModule>::const_reverse_iterator i = modules.rbegin();i != modules.rend();i++) {
		const ZzModule& module = *i;

		module._uninit_();

		if(__zz_log__::QCAP_LOG_LEVEL <= 3) { // LOGD
			printf("\033[1;34mModule %s uninit...\033[0m\n", module.name);
		}
	}
}
