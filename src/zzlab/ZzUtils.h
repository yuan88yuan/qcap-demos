#ifndef __ZZ_UTILS_H__
#define __ZZ_UTILS_H__

#include <stack>
#include <sys/mman.h>
#include <sys/epoll.h>

#include <boost/thread.hpp>
#include <boost/asio.hpp>
#include <boost/thread/tss.hpp>
#include <boost/unordered_map.hpp>
#include <boost/function.hpp>
#include <boost/filesystem.hpp>

#define container_of(ptr, type, member) ({ \
	void* __mptr = (void*)(ptr); \
	((type *)((char *)__mptr - offsetof(type, member))); \
})

#define ZZ_ENV_INIT(NAME, DEFVAL) \
ZzUtils::InitEnvParam(#NAME, &NAME, DEFVAL)

#define ZZ_CONCAT_I(N, S) N ## S
#define ZZ_CONCAT(N, S) ZZ_CONCAT_I(N, S)
#define ZZ_GUARD_NAME ZZ_CONCAT(__GUARD, __LINE__)

namespace ZzUtils {
#if 1 // NEW_ZZ_ENV
	void InitEnvParam(const char* strName, const char** pEnvParam, const char* strDefValue);
	void InitEnvParam(const char* strName, uint32_t* pEnvParam, const uint32_t& nDefValue);
	void InitEnvParam(const char* strName, int* pEnvParam, const int& nDefValue);
	void InitEnvParam(const char* strName, bool* pEnvParam, const bool& bDefValue);
#else
	inline bool GetEnvFlag(const char* name, bool def_value) {
		char* strval = getenv(name);
		return strval ? atoi(strval) : def_value;
	}

	int GetEnvIntValue(const char* name, int def_value);

	inline const char* GetEnvString(const char* name, const char* def_value) {
		char* strval = getenv(name);
		return strval ? strval : def_value;
	}
#endif

	bool StrToNumber(const char *str, uint32_t* pulValue);
	uint32_t ParseNumber(const char *str, uint32_t nDefValue);
	uint64_t ParseNumber64(const char *str, uint64_t nDefValue);
	void Fill_U32(uint32_t* pDst, int nSize, uint32_t nValue);
	bool StartsWith(const char* a, const char* b);
	void SetNextTimer(int fd_timer, int64_t duration);
	bool ReadAllBytes(const char* path, std::string& data);

	void* memory_map(uintptr_t base_addr, size_t* length, void** ppMapBase);

	inline void memory_unmap(void *base_addr, size_t length) {
		munmap(base_addr, length);
	}

	void uio_enable_irq(int fd, bool enable);
	int begin_cpu_access(int fd, int flags);
	int end_cpu_access(int fd, int flags);

	const int SCALE_RSHIFT = 5;
	const int SCALE_BASE = (1 << SCALE_RSHIFT);

	void mix_audio_16s(int16_t* pSrc, int16_t* pDst, int samples);
	void audio_scale_16s(int16_t* pSrc, int16_t* pDst, int samples, int16_t volume);
	void volume_db_16s(short* data, int nb_samples, int iChNum, double* pVolumeDB);
	void volume_db_16s(short* data, int nb_samples, double* pVolumeDB);
	void volume_db_32f(float* data, int nb_samples, double* pVolumeDB);

	struct audio_sampler {
		virtual void sample(uint8_t** pSrc, int src_offset_samples, uint8_t** pDst, int dst_offset_samples, int samples) = 0;
	};

	struct audio_dup_u8c1 : public audio_sampler {
		virtual void sample(uint8_t** pSrc, int src_offset_samples, uint8_t** pDst, int dst_offset_samples, int samples);
	};

	struct audio_dup_u8c2 : public audio_sampler {
		virtual void sample(uint8_t** pSrc, int src_offset_samples, uint8_t** pDst, int dst_offset_samples, int samples);
	};

	struct audio_dup_s16c1 : public audio_sampler {
		virtual void sample(uint8_t** pSrc, int src_offset_samples, uint8_t** pDst, int dst_offset_samples, int samples);
	};

	struct audio_dup_s16c2 : public audio_sampler {
		virtual void sample(uint8_t** pSrc, int src_offset_samples, uint8_t** pDst, int dst_offset_samples, int samples);
	};

	struct audio_dup_s32c1 : public audio_sampler {
		virtual void sample(uint8_t** pSrc, int src_offset_samples, uint8_t** pDst, int dst_offset_samples, int samples);
	};

	struct audio_dup_s32c2 : public audio_sampler {
		virtual void sample(uint8_t** pSrc, int src_offset_samples, uint8_t** pDst, int dst_offset_samples, int samples);
	};

	struct audio_dup_s16p2 : public audio_sampler {
		virtual void sample(uint8_t** pSrc, int src_offset_samples, uint8_t** pDst, int dst_offset_samples, int samples);
	};

	struct audio_scaler_s16c1 : public audio_sampler {
		int16_t volume;

		audio_scaler_s16c1(int16_t volume);
		virtual void sample(uint8_t** pSrc, int src_offset_samples, uint8_t** pDst, int dst_offset_samples, int samples);
	};

	struct audio_scaler_s16c2 : public audio_sampler {
		int16_t volume;

		audio_scaler_s16c2(int16_t volume);
		virtual void sample(uint8_t** pSrc, int src_offset_samples, uint8_t** pDst, int dst_offset_samples, int samples);
	};

	struct audio_scaler_s16p2 : public audio_sampler {
		int16_t volume;
		int16_t volume2;

		audio_scaler_s16p2(int16_t volume, int16_t volume2);
		virtual void sample(uint8_t** pSrc, int src_offset_samples, uint8_t** pDst, int dst_offset_samples, int samples);
	};

	struct audio_dup_fltc1 : public audio_sampler {
		virtual void sample(uint8_t** pSrc, int src_offset_samples, uint8_t** pDst, int dst_offset_samples, int samples);
	};

	struct audio_dup_fltc2 : public audio_sampler {
		virtual void sample(uint8_t** pSrc, int src_offset_samples, uint8_t** pDst, int dst_offset_samples, int samples);
	};

	struct audio_dup_fltp2 : public audio_sampler {
		virtual void sample(uint8_t** pSrc, int src_offset_samples, uint8_t** pDst, int dst_offset_samples, int samples);
	};

	inline int clamp_255(int a) {
		return (a < 0 ? 0 : (a > 255 ? 255 : a));
	}

	void rgb_yuv_p(int rgb[], int yuv[]);
	void yuv_full_limited_p(int yuv_full[], int yuv_limited[]);
	void yuv_limited_full_p(int yuv_limited[], int yuv_full[]);
	uint32_t rgb_yuv(uint32_t nColor);
	void rgb_gbr_p(int rgb[], int gbr[]);
	uint32_t rgb_gbr(uint32_t nColor);
	uint32_t rgb_p(int rgb[]);

	inline static uint32_t fourcc(char a, char b, char c, char d) {
		return (uint32_t)a | ((uint32_t)b << 8) | ((uint32_t)c << 16) | ((uint32_t)d << 24);
	}

	bool strstri(const char* src, const char* sub);

#if BUILD_HISIV
	bool writel(uintptr_t addr, uint32_t value);
	uint32_t readl(uintptr_t addr);
#endif

	void Set_8u_P4R(uint8_t* pDst[4], int dstStep[4], int nWidth, int nHeight, uint8_t pValue[4]);

	// H264/HEVC helper
	int FindNextStartCode(const uint8_t *buf, const uint8_t *next);
	bool ParseH264ExtraData(uint8_t* buffer, int size, std::vector<uint8_t>& extra_data);
	bool ParseH265ExtraData(uint8_t* buffer, int size, std::vector<uint8_t>& extra_data);
	bool ParseAV1ExtraData(uint8_t* buffer, int size, std::vector<uint8_t>& extra_data);
	void PrintExtraData(const uint8_t* extra_data, int extra_data_size);

	inline void PrintExtraData(const std::vector<uint8_t>& extra_data) {
		PrintExtraData(&extra_data[0], extra_data.size());
	}

	bool IsH264KeyFrame(uint8_t* buffer, int size);
	bool IsH265KeyFrame(uint8_t* buffer, int size);

	// ADTS/AAC helper
	void GetADTSConfig(int nObjectType, int nSampleFrequency, int nChannels, std::vector<uint8_t>& config);
	bool ParseADTSExtraData(uint8_t* buffer, int size, std::vector<uint8_t>& extra_data);

	template<typename T>
	struct is_in_vector {
		std::vector<T>& v;

		is_in_vector(std::vector<T>& v) : v(v) {}

		bool operator()(T a) {
			typename std::vector<T>::const_iterator i = std::find(v.begin(), v.end(), a);
			return i != v.end();
		}
	};

	void FourCCToStr(uint32_t v, std::string& ret);

	int NextId();

	struct ref_counter {
		boost::atomic<uint32_t> c;

		ref_counter() : c(1) {}
		~ref_counter() {}

		void add_ref() { c++; }
		uint32_t release() { return --c; }
	};

	template<class T>
	struct ref_counted {
		ref_counted() {}
		~ref_counted() {}

		void intrusive_ptr_add_ref() { c.add_ref(); }
		void intrusive_ptr_release() {
			if(c.release() == 0)
				delete static_cast<T*>(this);
		}

	private:
		ref_counter c;
	};

	struct FPSControl {
		double src_fps;
		double dst_fps;

		explicit FPSControl() {
		}

		void Reset() {
			mState = 0;
		}

		bool Tick() {
			mState += dst_fps;
			if(mState < src_fps)
				return false;

			mState -= src_fps;
			return true;
		}

	protected:
		double mState;
	};

	struct FPSControl1 {
		double dst_fps;

		void Reset() {
			mIsFirstFrame = true;
		}

		bool Tick(int64_t now);

	protected:
		bool mIsFirstFrame;
		int64_t mFirstFrameTime;
	};

	struct FPSControl2 {
		int num;
		int den;

		explicit FPSControl2() {
		}

		void Start(int64_t t) {
			mNum = num;
			mDenSecs = den * 1000000LL;
			mTimer = t;
		}

		int64_t Advance(int64_t t) {
			int64_t nDiff = t - mTimer;
			int64_t nTicks = nDiff * mNum / mDenSecs;
			int64_t nNextTimer = mTimer + (nTicks + 1) * mDenSecs / mNum;
			int64_t nAdvance = (nNextTimer - t);

			mTimer += nTicks * mDenSecs / mNum;

			return nAdvance;
		}

	protected:
		int64_t mNum;
		int64_t mDenSecs;
		int64_t mTimer;
	};

	struct SimpleKalmanFilter {
		double err_measure;
		double err_estimate;
		double noise_q;

		explicit SimpleKalmanFilter() {

		}

		~SimpleKalmanFilter() {

		}

		double update(double mesurement) {
			kalman_gain = err_estimate/(err_estimate + err_measure);
			current_estimate = last_estimate + kalman_gain * (mesurement - last_estimate);
			err_estimate =  (1.0 - kalman_gain)*err_estimate + fabs(last_estimate-current_estimate) * noise_q;
			last_estimate = current_estimate;

			return current_estimate;
		}

		double current_estimate;
		double last_estimate;
		double kalman_gain;
	};

	/* 1 - exp(-x) using a 3-order power series */
	inline double qexpneg(double x)
	{
		return 1 - 1 / (1 + x * (1 + x / 2 * (1 + x / 3)));
	}

	struct GenericClock {
		explicit GenericClock();
		virtual ~GenericClock();

		virtual int64_t tick(int64_t now, int64_t samples) = 0;
	};

	struct StepClock : public GenericClock {
		explicit StepClock();
		virtual ~StepClock();

		virtual int64_t tick(int64_t now, int64_t samples);
	};

	struct TimeFilter {
		// Delay Locked Loop data. These variables refer to mathematical
		// concepts described in: http://www.kokkinizita.net/papers/usingdll.pdf
		double cycle_time;
		double feedback2_factor;
		double feedback3_factor;
		double clock_period;
		int count;

		TimeFilter(double time_base, double period, double bandwidth);

		void reset() {
			count = 0;
		}

		double update(double system_time, double period);

		double eval(double delta) {
			return cycle_time + clock_period * delta;
		}
	};

	struct SmoothClock : public GenericClock, TimeFilter {
		int64_t mLastSamples;

		explicit SmoothClock(double time_base, double period, double bandwidth);
		virtual ~SmoothClock();

		virtual int64_t tick(int64_t now, int64_t samples);
	};

	struct PerfectClock : public GenericClock {
		int64_t mSampleRateNum;
		int64_t mSampleRateDen;
		int64_t mTotalSamples;
		int64_t mTimeBase;

		explicit PerfectClock(int64_t sample_rate_num, int64_t sample_rate_den);
		virtual ~PerfectClock();

		virtual int64_t tick(int64_t now, int64_t samples);
	};

	struct dmabuf_info_t {
		int index;
		void* ptr;
		size_t size;
		int fd;

		explicit dmabuf_info_t(const dmabuf_info_t& o);
		explicit dmabuf_info_t();
		~dmabuf_info_t();
	};

	struct DMABuffer {
		enum direction {
			DMA_BIDIRECTIONAL = 0,
			DMA_TO_DEVICE = 1,
			DMA_FROM_DEVICE = 2,
			DMA_NONE = 3,
		};

		int dmabuf_fd;
		size_t dmabuf_size;

		void* pVirAddr;
		uintptr_t nPhyAddr;
		size_t nSize;

#if 0 // DEPRECATED
		bool bShared;
#endif

		explicit DMABuffer(int nSize, bool bMapped = true, direction nDir = DMA_BIDIRECTIONAL);
#if 0 // DEPRECATED
		explicit DMABuffer(void* pVirAddr, uintptr_t nPhyAddr, size_t nSize);
		explicit DMABuffer(const DMABuffer* pBuffer, int nOffset = 0);
#endif // DEPRECATED
		~DMABuffer();

		int BeginCpuAccess(int nFlags) {
			if(dmabuf_fd != -1) {
				return begin_cpu_access(dmabuf_fd, nFlags);
			}

			return 0;
		}

		int EndCpuAccess(int nFlags) {
			if(dmabuf_fd != -1) {
				return end_cpu_access(dmabuf_fd, nFlags);
			}

			return 0;
		}

#if 0 // DEPRECATED
		void MakeShared(const DMABuffer* pBuffer, int nOffset = 0) {
			this->dmabuf_fd = pBuffer->dmabuf_fd;
			this->dmabuf_size = pBuffer->dmabuf_size;
			this->pVirAddr = (void*)((uintptr_t)pBuffer->pVirAddr + nOffset);
			this->nPhyAddr = pBuffer->nPhyAddr + nOffset;
			this->nSize = pBuffer->nSize;

			bShared = true;
		}
#endif // DEPRECATED
	};

	struct UserMM {
		size_t mapped_size;
		void* base_addr;
		void* high_addr;

		int mFD;

		explicit UserMM(uintptr_t nBaseAddr, uintptr_t nHighAddr, bool bReadOnly = false);
		~UserMM();

		uint32_t Read(size_t nOffset) {
			return *(uint32_t*)((uintptr_t)base_addr + nOffset);
		}

		void Write(size_t nOffset, uint32_t nValue) {
			*(uint32_t*)((uintptr_t)base_addr + nOffset) = nValue;
		}
	};

	struct QFrameBufferIRQ {
		typedef QFrameBufferIRQ self_t;

		boost::function<void (uint32_t)> on_event;

		int mFD;
		int mIRQType;
		std::string mResName;

		uintptr_t mBaseAddr;
		uintptr_t mHighAddr;

		boost::shared_ptr<boost::asio::posix::stream_descriptor> mEvent;
		uint32_t mEventData;

		explicit QFrameBufferIRQ(int nIRQType, const char* strResName, uintptr_t nBaseAddr, uintptr_t nHighAddr);
		~QFrameBufferIRQ();

		int GetFD() const {
			return mFD;
		}

		void Start(boost::asio::io_context& io_context_, int nMaxFrames, int nFrameType = 0);
		void Stop();

		void SetFrameBuffer(int nIndex, uintptr_t nPhyAddr, int nOffset[4]);
		void IssueFrameBuffer(int nIndex);

		void ReadNextEvent();
		void OnHandleEvent(const boost::system::error_code& error, size_t bytes_transferred);
	};

	struct Singleton {
		explicit Singleton(const char* name);
		~Singleton();

		bool operator!() {
			return pid_file_ == -1;
		}

		int pid_file_;
	};

	template<class T>
	struct TLSHashMap {
		typedef T object_t;

	public:
		template<class C> static object_t* get(C creator_, size_t nMaxObjects) {
			object_map_t& map_ = get_map();

			size_t nHashValue = ~0;
			creator_.hash(nHashValue);
			typename object_map_t::iterator i = map_.find(nHashValue);

			object_t* pObject;
			if(i == map_.end()) {
				if(map_.size() > nMaxObjects) {
					typename object_map_t::iterator i = map_.begin();
					delete i->second;

					map_.erase(i);
				}

				pObject = creator_();
				map_.emplace(nHashValue, pObject);
			} else {
				pObject = i->second;
			}

			return pObject;
		}

		static size_t get_cache_count() {
			object_map_t& map_ = get_map();

			return map_.size();
		}

	protected:
		typedef boost::unordered_map<size_t, object_t*> object_map_t;

		static void cleanup_map(object_map_t* data) {
			if(! data) return;

			for(typename object_map_t::const_iterator i = data->begin();i != data->end();i++) {
				delete i->second;
			}
			delete data;
		}

		static object_map_t& get_map() {
			static boost::thread_specific_ptr<object_map_t> value(cleanup_map);

			object_map_t* pObjectMap = value.get();
			if(! pObjectMap) {
				pObjectMap = new object_map_t();
				value.reset(pObjectMap);
			}

			return *pObjectMap;
		}
	};

	template<class T>
	struct TLSSingleton {
		typedef T object_t;

	public:
		static object_t& get() {
			static boost::thread_specific_ptr<object_t> value(cleanup_object);

			object_t* pObject = value.get();
			if(! pObject) {
				pObject = new object_t();
				value.reset(pObject);
			}

			return *pObject;
		}

	protected:
		static void cleanup_object(object_t* data) {
			if(! data) return;

			delete data;
		}
	};

	struct ThreadedParallels {
		boost::asio::io_context io_context_;
		boost::asio::io_service::work work;
		boost::asio::thread_pool thread_pool_;

		typedef boost::function<void(int, int)> executor_t;

		struct exec_slice_t {
			const executor_t& executor_;
			int begin_;
			int end_;

			boost::mutex& mutex_;
			boost::condition_variable& cv_;
			int& count_;

			exec_slice_t(const executor_t& executor, int begin, int end, boost::mutex& mutex, boost::condition_variable& cv, int& count);
			exec_slice_t(const exec_slice_t& a);

			void operator()();
		};

		int slices_;
		boost::mutex mutex_;
		boost::condition_variable cv_;
		int count_;

		ThreadedParallels(int nThreads);
		~ThreadedParallels();

		void Execute(const executor_t& executor, int works);

		static ThreadedParallels* Get(int nThreads = 0);
	};

	const int& get_qdmabuf_fd();

	struct Epolls {
		int fd_epoll;

		Epolls();
		~Epolls();

		int Add(int fd, int events) {
			struct epoll_event ev;
			ev.events = events;
			ev.data.fd = fd;
			return epoll_ctl(fd_epoll, EPOLL_CTL_ADD, fd, &ev);
		}

		int Del(int fd) {
			return epoll_ctl(fd_epoll, EPOLL_CTL_DEL, fd, NULL);
		}

		int Wait(epoll_event* events, int nevents, int timeout = -1) {
			return epoll_wait(fd_epoll, events, nevents, timeout);
		}
	};

	std::size_t hash(const char* s);

	struct FreeStack : protected std::stack<boost::function<void ()> > {
		typedef FreeStack self_t;
		typedef std::stack<boost::function<void ()> > parent_t;

		FreeStack();
		~FreeStack();

		template<class FUNC>
		self_t& operator +=(const FUNC& func) {
			push(func);

			return *this;
		}

		void Flush();
	};

	enum
	{
		OBU_SEQUENCE_HEADER = 1,
		OBU_TEMPORAL_DELIMITER = 2,
		OBU_FRAME = 6,
	};

#if BUILD_WITH_IREADER
	struct AV1ExtraDataParser {
		typedef AV1ExtraDataParser self_t;

		explicit AV1ExtraDataParser();
		virtual ~AV1ExtraDataParser();

		aom_av1_t oAV1;

		bool Parse(uint8_t* pBuffer, int nSize);

		bool mHasSequenceHeader;

		static int _av1_handler(void* param, const uint8_t* obu, size_t bytes);
		int av1_handler(const uint8_t* obu, size_t bytes);
	};
#endif

	struct URISplit {
		std::string scheme;
		std::string hier_part;
		std::string query;
		std::string fragment;

		// from hier_part
		std::string authority;
		std::string path;

		// from authority
		std::string userinfo;
		std::string host;
		std::string port;

		// from userinfo
		std::string user;
		std::string pass;

		std::string uri;

		explicit URISplit(const char* strURI);
		~URISplit();
	};

	void ExportGpio(int nPin, bool bOut);
	boost::filesystem::path GetGpioValuePath(int nPin, bool bOut);
	void BypassSwitch(int nPin, bool bByPass);
	void ResetIP(int nPin, useconds_t usec = 10000);

	struct Scoped {
		boost::function<void()> d;

		explicit Scoped(const boost::function<void()>& d) : d(d) {
		}

		~Scoped() {
			d();
		}
	};
}

template<typename FUNC> boost::thread ZzCreateThread(FUNC func, int priority) {
#if BUILD_HISIV
	boost::thread::attributes attrs;
	pthread_attr_setschedpolicy(attrs.native_handle(), SCHED_RR);
	sched_param param = { .sched_priority = priority };
	pthread_attr_setschedparam(attrs.native_handle(), &param);
	pthread_attr_setinheritsched(attrs.native_handle(), PTHREAD_EXPLICIT_SCHED);

	return boost::thread(attrs, func);
#else
	return boost::thread(func);
#endif
}

template<class LOCK>
struct ZzScopedLock {
	LOCK& lock;

	ZzScopedLock(LOCK& lock) : lock(lock) {
		lock.Lock();
	}

	~ZzScopedLock() {
		lock.Unlock();
	}
};

#endif // __ZZ_UTILS_H__
