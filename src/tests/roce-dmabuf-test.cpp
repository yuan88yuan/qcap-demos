#include <stdlib.h>
#include <time.h>

#include "qcap2.h"
#include "qcap2.cuda.h"

#include "ZzLog.h"
#include "ZzModules.h"
#include "ZzClock.h"
#include "ZzStats.h"
#include "ZzUtils.h"
#include "testkit.h"

ZZ_INIT_LOG("roce-dmabuf-test");

int g_argc = 0;
char** g_argv = NULL;

ZZ_MODULE_DECL(__zz_log__);

using namespace __zz_clock__;

/**
 * CUDA driver API error check helper
 */
#define CudaCheck(FUNC)                                                         \
	{                                                                           \
		const CUresult result = FUNC;                                           \
		if (result != CUDA_SUCCESS) {                                           \
			const char* error_name = "";                                        \
			cuGetErrorName(result, &error_name);                                \
			const char* error_string = "";                                      \
			cuGetErrorString(result, &error_string);                            \
			std::stringstream buf;                                              \
			buf << "[" << __FILE__ << ":" << __LINE__ << "] CUDA driver error " \
				<< result << " (" << error_name << "): " << error_string;       \
			throw std::runtime_error(buf.str().c_str());                        \
		}                                                                       \
	}

namespace __roce_dmabuf_test_ {
	ZZ_MODULES_INIT();
	struct modules_init_t;

	struct modules_init_t {
		modules_init_t() {
			ZZ_MODULE_INIT(__zz_log__);
		}

		~modules_init_t() {
			ZZ_MODULES_UNINIT();
		}
	};

	const int PAGE_SIZE = 128;
	const uint32_t METADATA_SIZE = 128;

	inline size_t align_8(size_t value) { return (value + 7) & ~7; }

	size_t round_up(size_t value, size_t alignment)
	{
		if (alignment == 0) {
			throw std::runtime_error("Alignment must be greater than zero");
		}
		return ((value + alignment - 1) / alignment) * alignment;
	}

	template <typename T>
	class Nullable {
	public:
		Nullable(T value = 0)
			: value_(value)
		{
		}
		Nullable(std::nullptr_t)
			: value_(0)
		{
		}
		operator T() const { return value_; };
		explicit operator bool() { return value_ != 0; }

		friend bool operator==(Nullable l, Nullable r) { return l.value_ == r.value_; }
		friend bool operator!=(Nullable l, Nullable r) { return !(l == r); }

		/**
		 * Deleter, call the function when the object is deleted.
		 *
		 * @tparam F function to call
		 */
		template <typename RESULT, RESULT func(T)>
		struct Deleter {
			typedef Nullable<T> pointer;
			void operator()(T value) const { func(value); }
		};

	private:
		T value_;
	};

	using UniqueCUdeviceptr = std::unique_ptr<Nullable<CUdeviceptr>, Nullable<CUdeviceptr>::Deleter<CUresult, &cuMemFree>>;
	struct CuHostPtrDeleter {
		void operator()(void* p) const { cuMemFreeHost(p); }
	};
	using UniqueCUhostptr = std::unique_ptr<void, CuHostPtrDeleter>;

	class ReceiverMemoryDescriptor {
	public:
		/**
		 * Allocate a region of GPU memory which will be page
		 * aligned and freed on destruction.
		 */
		explicit ReceiverMemoryDescriptor(CUcontext context, size_t size);
		ReceiverMemoryDescriptor() = delete;
		~ReceiverMemoryDescriptor();

		CUdeviceptr get() { return mem_; };

	protected:
		UniqueCUdeviceptr deviceptr_;
		UniqueCUhostptr host_deviceptr_;
		CUdeviceptr mem_;
	};

	ReceiverMemoryDescriptor::ReceiverMemoryDescriptor(CUcontext cu_context, size_t size)
	{
		CudaCheck(cuInit(0));
		CudaCheck(cuCtxSetCurrent(cu_context));
		CUdevice device;
		CudaCheck(cuCtxGetDevice(&device));
		int integrated = 0;

		// Add enough space to guarantee that the pointer we return
		// can be page aligned.
		size_t page_size = getpagesize();
		size_t page_mask = page_size - 1;
		// Make sure size isn't gonna overflow
		if (size > std::numeric_limits<size_t>::max() - page_size) {
			// throw std::overflow_error(fmt::format("Requested buffer size={:#x} is too large for page-aligned allocation", size));
		}
		size_t allocation_size = size + page_size;

		CudaCheck(cuDeviceGetAttribute(&integrated, CU_DEVICE_ATTRIBUTE_INTEGRATED, device));

		LOGD("integrated=%d", integrated);
		if (integrated == 0) {
			// We're a discrete GPU device; so allocate using cuMemAlloc/cuMemFree
			deviceptr_.reset([allocation_size] {
				CUdeviceptr device_deviceptr;
				CudaCheck(cuMemAlloc(&device_deviceptr, allocation_size));
				return device_deviceptr;
			}());
			CUdeviceptr mem = deviceptr_.get();
			// Round up size to page boundary;
			// Note deviceptr_ is what we'll free; don't try to free mem_.
			size_t rem = mem & page_mask;
			if (rem) {
				mem += (page_size - rem);
			}
			mem_ = mem;
		} else {
			// We're an integrated device (e.g. Tegra) so we must allocate
			// using cuMemHostAlloc/cuMemFreeHost
			host_deviceptr_.reset([allocation_size] {
				void* host_deviceptr;
				unsigned int flags = CU_MEMHOSTALLOC_DEVICEMAP;
				CudaCheck(cuMemHostAlloc(&host_deviceptr, allocation_size, flags));
				return host_deviceptr;
			}());

			CUdeviceptr device_deviceptr;
			CudaCheck(cuMemHostGetDevicePointer(&device_deviceptr, host_deviceptr_.get(), 0));
			CUdeviceptr mem = device_deviceptr;
			// Round up size to page boundary;
			// Note: host_deviceptr_ is what we'll free; don't try to free mem_.
			size_t rem = mem & page_mask;
			if (rem) {
				mem += (page_size - rem);
			}
			mem_ = mem;
		}
	}

	ReceiverMemoryDescriptor::~ReceiverMemoryDescriptor()
	{
		host_deviceptr_.release();
		deviceptr_.release();
	}

}

using namespace __roce_dmabuf_test_;
using __testkit__::wait_for_test_finish;
using __testkit__::TestCase;
using __testkit__::free_stack_t;
using __testkit__::tick_ctrl_t;
using __testkit__::NewEvent;
using __testkit__::AddEventHandler;

struct App0 {
	typedef App0 self_t;

	static int Main() {
		self_t app;
		return app.Run();
	}

	bool running;

	int Run() {
		int TEST_CASE;
		ZZ_ENV_INIT(TEST_CASE, 0);

		if(TEST_CASE != 0) {
			DoCase(TEST_CASE);

			return 0;
		}

		running = true;
		LOGI("Press 'q' to exit.");
		while(running) {
			char ch;

			int fd_stdin = 0; // stdin

			fd_set readfds;
			FD_ZERO(&readfds);

			int fd_max = -1;
			if(fd_stdin > fd_max) fd_max = fd_stdin;
			FD_SET(fd_stdin, &readfds);

			int err = select(fd_max + 1, &readfds, NULL, NULL, NULL);
			if (err < 0) {
				LOGE("%s(%d): select() failed! err = %d", __FUNCTION__, __LINE__, err);
				break;
			}

			if (FD_ISSET(fd_stdin, &readfds)) {
				ch = getchar();

				switch(ch) {
				case 'q':
					running = false;
					break;

				case '0':
				case '1':
				case '2':
				case '3':
				case '4':
				case '5':
				case '6':
				case '7':
				case '8':
				case '9':
					DoCase(ch - '0');
					break;
				}
			}
		}

		return 0;
	}

	void DoCase(int t) {
		LOGD("DoCase(%d)", t);

		switch(t) {
		case 1:
			mTestCase1.DoWork();
			break;
		}
	}

	struct TestCase1 : public TestCase {
		typedef TestCase1 self_t;
		typedef TestCase super_t;

		CUdevice cu_device;
		CUcontext cu_context;

		void DoWork() {
			QRESULT qres;
			int err;
			CUresult cures;

			LOGD("%s::%s", typeid(self_t).name(), __FUNCTION__);

			switch(1) { case 1:
				free_stack_t& _FreeStack_ = _FreeStack_main_;

				const int cu_device_ordinal = 0;

				cures = cuInit(0);
				if(cures != CUDA_SUCCESS) {
					qres = QCAP_RS_ERROR_GENERAL;
					LOGE("%s(%d): cuInit() failed, cures=%d", __FUNCTION__, __LINE__, cures);
					break;
				}
				LOGD("cuInit()...");

				cures = cuDeviceGet(&cu_device, cu_device_ordinal);
				if(cures != CUDA_SUCCESS) {
					qres = QCAP_RS_ERROR_GENERAL;
					LOGE("%s(%d): cuDeviceGet() failed, cures=%d", __FUNCTION__, __LINE__, cures);
					break;
				}
				LOGD("cuDeviceGet()...");

				cures = cuDevicePrimaryCtxRetain(&cu_context, cu_device);
				if(cures != CUDA_SUCCESS) {
					qres = QCAP_RS_ERROR_GENERAL;
					LOGE("%s(%d): cuDevicePrimaryCtxRetain() failed, cures=%d", __FUNCTION__, __LINE__, cures);
					break;
				}
				_FreeStack_ += [&]() {
					CUresult cures;

					cures = cuDevicePrimaryCtxRelease(cu_device);
					if(cures != CUDA_SUCCESS) {
						LOGE("%s(%d): cuDevicePrimaryCtxRelease() failed, cures=%d", __FUNCTION__, __LINE__, cures);
					}
				};
				LOGD("cuDevicePrimaryCtxRetain()...");

				const unsigned PAGES = 2;
				int width = 3840;
				int height = 2160;
				int frame_start_size_ = 0;
				int frame_end_size_ = 0;
				int line_start_size_ = 0;
				int line_end_size_ = 0;
				int bytes_per_line_ = width * 2;
				const uint32_t line_size = line_start_size_ + bytes_per_line_ + line_end_size_;
				int  frame_size_ = align_8(frame_start_size_ + line_size * height + frame_end_size_);
				LOGD("frame_size_=%d", frame_size_);

				LOGD("PAGE_SIZE=%d", PAGE_SIZE);

				// int _PAGE_SIZE = getpagesize();
				int _PAGE_SIZE = PAGE_SIZE;
				LOGD("_PAGE_SIZE=%d", _PAGE_SIZE);

				size_t metadata_address = round_up(frame_size_, PAGE_SIZE);
				size_t received_frame_size = metadata_address + METADATA_SIZE;
				size_t buffer_size = round_up(received_frame_size * PAGES, getpagesize());

			    std::unique_ptr<ReceiverMemoryDescriptor> frame_memory_;
				frame_memory_.reset(new ReceiverMemoryDescriptor(cu_context, buffer_size));

				CUdeviceptr buffer_ = frame_memory_->get();
				LOGD("buffer_=%p", buffer_);

				int fd_tmp = -1;
				LOGW("%s(%d): +cuMemGetHandleForAddressRange(%d, %p, %u)", __FUNCTION__, __LINE__,
					fd_tmp, buffer_, buffer_size);
				cures = cuMemGetHandleForAddressRange(
					(void*)&fd_tmp, buffer_, buffer_size,
					CU_MEM_RANGE_HANDLE_TYPE_DMA_BUF_FD, 0);
				LOGW("%s(%d): -cuMemGetHandleForAddressRange(), cures=%d fd_tmp=%d", __FUNCTION__, __LINE__, cures, fd_tmp);
				if(cures == CUDA_SUCCESS) {
					close(fd_tmp);
				}
			}

			_FreeStack_main_.flush();
		}

	} mTestCase1;
};

int main(int argc, char* argv[]) {
	g_argc = argc;
	g_argv = argv;

	int ret;
	switch(1) { case 1:
		modules_init_t _modules_init_;

		ret = App0::Main();
	}

	return ret;
}
