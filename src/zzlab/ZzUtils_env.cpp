#include "ZzUtils.h"
#include "ZzLog.h"

ZZ_INIT_LOG("ZzUtils_env");

namespace __zz_utils_env__ {
	void _init_() {
	}

	void _uninit_() {
	}
}

namespace ZzUtils {
#if 1 // NEW_ZZ_ENV
	void InitEnvParam(const char* strName, const char** pEnvParam, const char* strDefValue) {
		char* strVal = getenv(strName);
		if(strVal) {
			*pEnvParam = strVal;
			LOGD("%s=%s", strName, *pEnvParam);
		} else {
			*pEnvParam = strDefValue;
			LOGD("*%s=%s", strName, *pEnvParam);
		}
	}

	void InitEnvParam(const char* strName, uint32_t* pEnvParam, const uint32_t& nDefValue) {
		char* strVal = getenv(strName);
		if(strVal) {
			uint32_t value;
			if(ZzUtils::StrToNumber(strVal, &value)) {
				*pEnvParam = value;
				LOGD("%s=%u", strName, *pEnvParam);
			} else {
				*pEnvParam = nDefValue;
				LOGD("*%s=%u", strName, *pEnvParam);
			}
		} else {
			*pEnvParam = nDefValue;
			LOGD("*%s=%u", strName, *pEnvParam);
		}
	}

	void InitEnvParam(const char* strName, int* pEnvParam, const int& nDefValue) {
		char* strVal = getenv(strName);
		if(strVal) {
			uint32_t value;
			if(ZzUtils::StrToNumber(strVal, &value)) {
				*pEnvParam = (int)value;
				LOGD("%s=%d", strName, *pEnvParam);
			} else {
				*pEnvParam = nDefValue;
				LOGD("*%s=%d", strName, *pEnvParam);
			}
		} else {
			*pEnvParam = nDefValue;
			LOGD("*%s=%d", strName, *pEnvParam);
		}
	}

	void InitEnvParam(const char* strName, bool* pEnvParam, const bool& bDefValue) {
		char* strVal = getenv(strName);
		if(strVal) {
			*pEnvParam = (atoi(strVal) == 0) ? false : true;
		} else {
			*pEnvParam = bDefValue;
		}

		if(*pEnvParam) {
			LOGD("%s", strName);
		}
	}
#else
	int GetEnvIntValue(const char* name, int def_value) {
		int ret = def_value;

		char* strValue = getenv(name);
		if(strValue) {
			uint32_t value;
			if(StrToNumber(strValue, &value)) {
				ret = (int)value;
			}
		}

		return ret;
	}
#endif
}

extern "C" {
	int qcap_get_env_int(const char* strName, int nDefValue) {
		int ret = nDefValue;

		char* strValue = getenv(strName);
		if(strValue) {
			uint32_t value;
			if(ZzUtils::StrToNumber(strValue, &value)) {
				ret = (int)value;
			}
		}

		return ret;
	}
}
