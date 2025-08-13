#ifndef __ZZ_LOG_H__
#define __ZZ_LOG_H__

#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>

namespace __zz_log__ {
	extern int QCAP_LOG_LEVEL;

	const static int QCAP_LOG_MAX_BUF_SIZE = 1024;
	const static int QCAP_LOG_LEVEL_VERBOSE = 2;
	const static int QCAP_LOG_LEVEL_DEBUG = 3;
	const static int QCAP_LOG_LEVEL_INFO = 4;
	const static int QCAP_LOG_LEVEL_WARN = 5;
	const static int QCAP_LOG_LEVEL_ERROR = 6;
	const static int QCAP_LOG_LEVEL_NOTICE = 8;

	int FormatLog(char* buf, size_t buf_size, const char* prefix, const char* fmt, va_list& marker);
}

#if ZZLOG_HIDE
#define __ZZ_LOG_IMPL__(LEVEL, PREFIX, FMT)
#else // ZZLOG_HIDE
#define __ZZ_LOG_IMPL__(LEVEL, PREFIX, FMT) \
		if(__zz_log__::QCAP_LOG_LEVEL > LEVEL) return; \
		char _buf[__zz_log__::QCAP_LOG_MAX_BUF_SIZE]; \
		va_list marker; \
		va_start(marker, FMT); \
		int _buf_end = __zz_log__::FormatLog(_buf, sizeof(_buf), PREFIX, FMT, marker); \
		va_end(marker); \
		fwrite(_buf, _buf_end, 1, stdout)
#endif // ZZLOG_HIDE

struct ZzLog {
	int level;
	const char* tag;
	const char* prefix;

	void operator() (const char* fmt, ...) {
		__ZZ_LOG_IMPL__(level, prefix, fmt);
	}
};

#define ZZ_INIT_LOG(TAG) \
	static ZzLog LOGV = {__zz_log__::QCAP_LOG_LEVEL_VERBOSE, TAG, "\033[0;37mVERBOSE[" TAG "]: "}; \
	static ZzLog LOGD = {__zz_log__::QCAP_LOG_LEVEL_DEBUG, TAG, "\033[0;36mDEBUG[" TAG "]: "}; \
	static ZzLog LOGI = {__zz_log__::QCAP_LOG_LEVEL_INFO, TAG, "\033[0mINFO[" TAG "]: "}; \
	static ZzLog LOGW = {__zz_log__::QCAP_LOG_LEVEL_WARN, TAG, "\033[1;33mWARN[" TAG "]: "}; \
	static ZzLog LOGE = {__zz_log__::QCAP_LOG_LEVEL_ERROR, TAG, "\033[1;31mERROR[" TAG "]: "}; \
	static ZzLog LOGN = {__zz_log__::QCAP_LOG_LEVEL_NOTICE, TAG, "\033[1;32m[" TAG "]: "}

#define TRACE_TAG() LOGI("\033[1;33m**** %s(%d)\033[0m", __FILE__, __LINE__)
#define TODO_TAG() LOGW("%s(%d): TODO", __FILE__, __LINE__)

#endif // __ZZ_LOG_H__