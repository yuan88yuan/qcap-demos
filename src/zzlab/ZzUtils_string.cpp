#include "ZzUtils.h"
#include "ZzLog.h"

ZZ_INIT_LOG("ZzUtils_string");

namespace __zz_utils_string__ {
	void _init_() {
	}

	void _uninit_() {
	}
}

namespace ZzUtils {
	template<class T> bool atoul(const char *str, T * pulValue)
	{
		static const T MAX_V = std::numeric_limits<T>::max() / 10;

		T ulResult=0;

		while (*str)
		{
			if (isdigit((int)*str))
			{
				if ((ulResult<MAX_V) || ((ulResult==MAX_V) && (*str<'6')))
				{
					ulResult = ulResult*10 + (*str)-48;
				}
				else
				{
					*pulValue = ulResult;
					return false;
				}
			}
			else
			{
				*pulValue=ulResult;
				return false;
			}
			str++;
		}
		*pulValue=ulResult;
		return true;
	}

	#define ASC2NUM(ch) (ch - '0')
	#define HEXASC2NUM(ch) (ch - 'A' + 10)

	template<class T> bool atoulx(const char *str, T * pulValue)
	{
		static const T MAX_V = std::numeric_limits<T>::max() >> 4 + 1;

		T   ulResult=0;
		uint8_t ch;

		while (*str)
		{
			ch=toupper(*str);
			if (isdigit(ch) || ((ch >= 'A') && (ch <= 'F' )))
			{
				if (ulResult < MAX_V)
				{
					ulResult = (ulResult << 4) + ((ch<='9')?(ASC2NUM(ch)):(HEXASC2NUM(ch)));
				}
				else
				{
					*pulValue=ulResult;
					return false;
				}
			}
			else
			{
				*pulValue=ulResult;
				return false;
			}
			str++;
		}

		*pulValue=ulResult;
		return true;
	}

	bool StrToNumber(const char *str , uint32_t * pulValue)
	{
		if ( *str == '0' && (*(str+1) == 'x' || *(str+1) == 'X') )
		{
			if (*(str+2) == '\0')
			{
				return false;
			}
			else
			{
				return atoulx<uint32_t>(str+2, pulValue);
			}
		}
		else
		{
			return atoul<uint32_t>(str, pulValue);
		}
	}

	bool StrToNumber64(const char *str , uint64_t * pulValue)
	{
		if ( *str == '0' && (*(str+1) == 'x' || *(str+1) == 'X') )
		{
			if (*(str+2) == '\0')
			{
				return false;
			}
			else
			{
				return atoulx<uint64_t>(str+2,pulValue);
			}
		}
		else
		{
			return atoul<uint64_t>(str,pulValue);
		}
	}

	uint32_t ParseNumber(const char *str, uint32_t nDefValue) {
		uint32_t nValue;

		return StrToNumber(str, &nValue) ? nValue : nDefValue;
	}

	uint64_t ParseNumber64(const char *str, uint64_t nDefValue) {
		uint64_t nValue;

		return StrToNumber64(str, &nValue) ? nValue : nDefValue;
	}

	bool StartsWith(const char* a, const char* b) {
		while(*a && *b) {
			if(*a++ != *b++)
				return false;
		}

		return *b == '\0';
	}

	void Fill_U32(uint32_t* pDst, int nSize, uint32_t nValue) {
		for(int i = 0;i < nSize;++i, ++pDst) {
			*pDst = nValue;
		}
	}

	bool strstri(const char* src, const char* sub) {
		while(*src) {
			const char* p0 = sub;
			const char* p1 = src;
			while(*p0 && *p1) {
				if(toupper(*p0) != toupper(*p1))
					break;

				p0++;
				p1++;
			}
			if(! *p0) {
				return true;
			}

			src++;
		}

		return false;
	}


	inline char FourCCChar(uint32_t a) {
		if((a >= 'a' && a <= 'z') || (a >= 'A' && a <= 'Z') || (a >= '0' && a <= '9'))
			return a;

		return '#';
	}

	void FourCCToStr(uint32_t v, std::string& ret) {
		char buf[32];
		switch(v) {
		case 0:
			sprintf(buf, "RGB24(0x%08X)", v);
			break;

		case 1:
			sprintf(buf, "BGR24(0x%08X)", v);
			break;

		case 2:
			sprintf(buf, "ARGB32(0x%08X)", v);
			break;

		case 3:
			sprintf(buf, "ABGR32(0x%08X)", v);
			break;

		default:
			sprintf(buf, "%c%c%c%c(0x%08X)",
				FourCCChar(v & 0xFF), FourCCChar((v >> 8) & 0xFF),
				FourCCChar((v >> 16) & 0xFF), FourCCChar((v >> 24) & 0xFF), v);
			break;
		}

		ret = buf;
	}
}
