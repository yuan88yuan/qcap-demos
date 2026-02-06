#include "ZzUtils.h"
#include "ZzLog.h"

ZZ_INIT_LOG("ZzUtils_memory");

namespace __zz_utils_memory__ {
	void _init_() {
	}

	void _uninit_() {
	}
}

namespace ZzUtils {
	void* memory_map(uintptr_t base_addr, size_t* length, void** ppMapBase) {
		int err;

		int fd = open("/dev/mem", O_RDWR | O_SYNC);
		if(fd == -1) {
			err = errno;
			LOGD("%s(%d): open failed", __FUNCTION__, __LINE__, err);
			return NULL;
		}

		unsigned page_size = getpagesize();
		unsigned offset_in_page = (unsigned)base_addr & (page_size - 1);
		// LOGD("page_size=%u offset_in_page=%u", page_size, offset_in_page);

		void* map_base = mmap(NULL, *length, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
			base_addr & ~(off_t)(page_size - 1));
		if(map_base == MAP_FAILED) {
			err = errno;
			LOGD("%s(%d): mmap failed", __FUNCTION__, __LINE__, err);

			close(fd);

			return NULL;
		}

		close(fd);

		*ppMapBase = map_base;

		return (void*)((uint8_t*)map_base + offset_in_page);
	}

	UserMM::UserMM(uintptr_t nBaseAddr, uintptr_t nHighAddr, bool bReadOnly) {
		size_t nSize = nHighAddr - nBaseAddr + 1;
		int nPageSize = getpagesize();
		int nPages = (nSize + (nPageSize - 1)) / nPageSize;
		mapped_size = nPages * nPageSize;

		mFD = open("/dev/mem", bReadOnly ? (O_RDONLY | O_SYNC) : (O_RDWR | O_SYNC));
		base_addr = mmap(NULL, mapped_size, (bReadOnly ? PROT_READ : (PROT_READ | PROT_WRITE)), MAP_SHARED, mFD, nBaseAddr);
		high_addr = (void*)((uintptr_t)base_addr + ((int)mapped_size - 1));

		LOGD("UserMM: %p ~ %p(0x%X) ==> fd=%d, %p ~ %p", nBaseAddr, nHighAddr, mapped_size, mFD, base_addr, high_addr);
	}

	UserMM::~UserMM() {
		int err;

		if(mFD != -1) {
			err = munmap(base_addr, mapped_size);
			if(err) {
				err = errno;
				LOGE("%s(%d): munmap failed", __FUNCTION__, __LINE__, err);
			}

			close(mFD);
		}
	}
}
