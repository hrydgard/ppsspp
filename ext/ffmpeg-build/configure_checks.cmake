# Before we compile ffmpeg libs, we need to generate config.h, (config.asm if enabling Assembly optimizations), libavutil/avconfig.h, and libavutil/ffversion.h.

include(configure_functions.cmake)

# Check for extern_prefix
check_cc(ffextern "int ff_extern;" HAVE_ff_extern)
execute_process(
    COMMAND nm "${CONFIG_TESTS_DIR}/ffextern.o"
    OUTPUT_VARIABLE NM_RESULT
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
file(WRITE "${CONFIG_TESTS_DIR}/nm_ff_extern" "${NM_RESULT}")
execute_process(
    COMMAND awk "/ff_extern/{ print substr($0, match($0, /[^ \t]*ff_extern/)) }" "${CONFIG_TESTS_DIR}/nm_ff_extern"
    OUTPUT_VARIABLE AWK_RESULT
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
set(extern_prefix_orig \"${AWK_RESULT}\")
string(REGEX REPLACE "ff_extern" "" extern_prefix "${extern_prefix_orig}")
set(extern_asm_orig "${AWK_RESULT}")
string(REGEX REPLACE "ff_extern" "" extern_asm "${extern_asm_orig}")

set(build_suffix \"\")

set(SLIBSUF \"${CMAKE_SHARED_LIBRARY_SUFFIX}\")

# See line 3055 of ffmpeg configure script, where 256 is hardcoded as the default
set(sws_max_filter_size 256)

# Check the restrict keyword and assign accordingly
set(_RESTRICT)
check_cc(restrict "void foo(char * restrict p);" restrict)
check_cc(__restrict__ "void foo(char * __restrict__ p);" __restrict__)
check_cc(__restrict "void foo(char * __restrict p);" __restrict)
if (restrict)
	set(_RESTRICT "restrict")
elseif (__restrict__)
	set(_RESTRICT "__restrict__")
elseif (__restrict)
	set(_RESTRICT "__restrict")
else()
	message(STATUS "restrict keyword not found!")
endif()
if (NOT _RESTRICT STREQUAL "restrict")
	check_cc(restrict_cflags "__declspec(${_RESTRICT}) void* foo(int);" stdlib_cflag)
	if (stdlib_cflag)
		# Primarily needed with MSVC
		add_compile_options(-FIstdlib.h)
	endif()
endif()

# Get the compiler ident string in the same format as ffmpeg's configure script--retaining only the first line
if (MSVC AND NOT (CMAKE_C_COMPILER_ID STREQUAL "Clang"))	# Real MSVC, not clang-cl emulating MSVC
	execute_process(
		COMMAND cl
		ERROR_VARIABLE CC_VERSION
		OUTPUT_STRIP_TRAILING_WHITESPACE
	)
else()	# clang (including clang-cl) and gcc
	execute_process(
    	COMMAND cc "--version"
    	OUTPUT_VARIABLE CC_VERSION
    	OUTPUT_STRIP_TRAILING_WHITESPACE
	)
endif()
string(REGEX REPLACE "\n.*" "" CC_IDENT "${CC_VERSION}")
set(CC_IDENT \"${CC_IDENT}\")

# Check for PIC - see line 4845 of ffmpeg's configure script
set(CONFIG_PIC 1)
#check_cpp_condition(pic "stdlib.h" "defined(__PIC__) || defined(__pic__) || defined(PIC)" CONFIG_PIC)
# Note: the above check PASSES if cross-compiling for Windows targets, but fails for FreeBSD host; enabling always

# Line 4933
check_cc(pragma_deprecated [[void foo(void) { _Pragma("GCC diagnostic ignored \"-Wdeprecated-declarations\"") }]] HAVE_PRAGMA_DEPRECATED)

check_cc(attribute_packed "struct { int x; } __attribute__((packed)) x;" HAVE_ATTRIBUTE_PACKED)

check_cc(attribute_may_alias "union { int x; } __attribute__((may_alias)) x;" HAVE_ATTRIBUTE_MAY_ALIAS)

check_func(dlopen "dlopen" HAVE_DLOPEN)

check_builtin(atomic_cas_ptr "<atomic.h>" "void **ptr; void *oldval, *newval; atomic_cas_ptr(ptr, oldval, newval)" HAVE_ATOMIC_CAS_PTR)
check_builtin(atomic_compare_exchange "" "int *ptr, *oldval; int newval; __atomic_compare_exchange_n(ptr, oldval, newval, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)" HAVE_ATOMIC_COMPARE_EXCHANGE)
check_builtin(machine_rw_barrier "<mbarrier.h>" "__machine_rw_barrier()" HAVE_MACHINE_RW_BARRIER)
check_builtin(MemoryBarrier "<windows.h>" "MemoryBarrier()" HAVE_MEMORYBARRIER)
check_builtin(sarestart "<signal.h>" "SA_RESTART" HAVE_SARESTART)
check_builtin(sync_val_compare_and_swap "" "int *ptr; int oldval, newval; __sync_val_compare_and_swap(ptr, oldval, newval)" HAVE_SYNC_VAL_COMPARE_AND_SWAP)
check_builtin(gmtime_r "<time.h>" "time_t *time; struct tm *tm; gmtime_r(time, tm)" HAVE_GMTIME_R)
check_builtin(localtime_r "<time.h>" "time_t *time; struct tm *tm; localtime_r(time, tm)" HAVE_LOCALTIME_R)

check_func_headers(aligned_malloc "<malloc.h>" "_aligned_malloc" "" HAVE_ALIGNED_MALLOC)
# Note: we don't have a 'custom_allocator' option set, so we don't need a malloc_prefix
check_func(memalign "memalign" HAVE_MEMALIGN)
if (NOT MINGW AND NOT MINGW_SYSROOT)	# First of many MinGW-specific conditionals to prevent false-positives
	check_func(posix_memalign "posix_memalign" HAVE_POSIX_MEMALIGN)
endif()
check_func(access "access" HAVE_ACCESS)
check_func_headers(arc4random "<stdlib.h>" "arc4random" "" HAVE_ARC4RANDOM)
check_func_headers(clock_gettime "<time.h>" "clock_gettime" "" HAVE_CLOCK_GETTIME)
if (NOT HAVE_CLOCK_GETTIME)
	check_func_headers(clock_gettime "<time.h>" "clock_gettime" "-lrt" HAVE_CLOCK_GETTIME)
		if (HAVE_CLOCK_GETTIME)
			add_link_options(-lrt)
		endif()
endif()
if (NOT MINGW AND NOT MINGW_SYSROOT)
	check_func(fcntl "fcntl" HAVE_FCNTL)
endif()
check_func(flt_lim "flt_lim" HAVE_FLT_LIM)	# Note: if this check fails, a second check is run later that can pass
check_func(fork "fork" HAVE_FORK)
if (NOT MINGW AND NOT MINGW_SYSROOT)
	check_func(gethrtime "gethrtime" HAVE_GETHRTIME)
endif()
check_func(getopt "getopt" HAVE_GETOPT)
check_func(getrusage "getrusage" HAVE_GETRUSAGE)
check_func(gettimeofday "gettimeofday" HAVE_GETTIMEOFDAY)
check_func(isatty "isatty" HAVE_ISATTY)
if (NOT MINGW AND NOT MINGW_SYSROOT)
	check_func(mach_absolute_time "mach_absolute_time" HAVE_MACH_ABSOLUTE_TIME)
endif()
check_func(mkstemp "mkstemp" HAVE_MKSTEMP)
if (NOT MINGW AND NOT MINGW_SYSROOT)
	check_func(mmap "mmap" HAVE_MMAP)
endif()
check_func(mprotect "mprotect" HAVE_MPROTECT)
check_func_headers(nanosleep "<time.h>" "nanosleep" "" HAVE_NANOSLEEP)
if (NOT HAVE_NANOSLEEP)
	check_func_headers(nanosleep "<time.h>" "nanosleep" "-lrt" HAVE_NANOSLEEP)
		if (HAVE_NANOSLEEP)
			add_link_options(-lrt)
		endif()
endif()
if (NOT MINGW AND NOT MINGW_SYSROOT)	# Failsafe against breaking MinGW builds at libavutil/cpu.c
	check_func(sched_getaffinity "sched_getaffinity" HAVE_SCHED_GETAFFINITY)
endif()
check_func(setrlimit "setrlimit" HAVE_SETRLIMIT)
check_struct(st_mtim.tv_nsec "<sys/stat.h>" "struct stat" st_mtim.tv_nsec HAVE_STRUCT_STAT_ST_MTIM_TV_NSEC "-D_DEFAULT_SOURCE")
# Note: -D_DEFAULT_SOURCE replaced -D_BSD_SOURCE as a flag to enable various extensions to POSIX in 2014
# Note 2: on Arch Linux at least, this is already defined in features.h, resulting in a -Wmacro-redefined warning
if (NOT MINGW AND NOT MINGW_SYSROOT)
	check_func(strerror_r "strerror_r" HAVE_STRERROR_R)
endif()
check_func(sysconf "sysconf" HAVE_SYSCONF)
if (NOT MINGW AND NOT MINGW_SYSROOT AND NOT CMAKE_SYSTEM_NAME MATCHES "SunOS")
	check_func(sysctl "sysctl" HAVE_SYSCTL)
endif()
check_func(usleep "usleep" HAVE_USLEEP)

check_func_headers(kbhit "<conio.h>" "kbhit" "" HAVE_KBHIT)
check_func_headers(setmode "<io.h>" "setmode" "" HAVE_SETMODE)
check_func_headers(lzo1x_999_compress "<lzo/lzo1x.h>" "lzo1x_999_compress" "" HAVE_LZO1X_999_COMPRESS)
check_func_headers(getenv "<stdlib.h>" "getenv" "" HAVE_GETENV)
check_func_headers(lstat "<sys/stat.h>" "lstat" "" HAVE_LSTAT)

# Note: lines 5318-5328 all rely on windows.h header--skip irrelevant checks if windows.h not found
check_header(windows "windows.h" "" HAVE_WINDOWS_H)
if (HAVE_WINDOWS_H)
	check_func_headers(cotaskmemfree "<windows.h>" "CoTaskMemFree" "-lole32" HAVE_COTASKMEMFREE)
	check_func_headers(getprocessaffinitymask "<windows.h>" "GetProcessAffinityMask" "" HAVE_GETPROCESSAFFINITYMASK)
	check_func_headers(getprocesstimes "<windows.h>" "GetProcessTimes" "" HAVE_GETPROCESSTIMES)
	check_func_headers(getsystemtimeasfiletime "<windows.h>" "GetSystemTimeAsFileTime" "" HAVE_GETSYSTEMTIMEASFILETIME)
	check_func_headers(mapviewoffile "<windows.h>" "MapViewOfFile" "" HAVE_MAPVIEWOFFILE)
	check_func_headers(peeknamedpipe "<windows.h>" "PeekNamedPipe" "" HAVE_PEEKNAMEDPIPE)
	check_func_headers(setconsoletextattribute "<windows.h>" "SetConsoleTextAttribute" "" HAVE_SETCONSOLETEXTATTRIBUTE)
	check_func_headers(setconsolectrlhandler "<windows.h>" "SetConsoleCtrlHandler" "" HAVE_SETCONSOLECTRLHANDLER)
	check_func_headers(sleep "<windows.h>" "Sleep" "" HAVE_SLEEP)
	check_func_headers(virtualalloc "<windows.h>" "VirtualAlloc" "" HAVE_VIRTUALALLOC)
	check_struct(condition_variable_ptr "<windows.h>" "CONDITION_VARIABLE" "Ptr" HAVE_CONDITION_VARIABLE_PTR)
endif()
check_func_headers(glob "<glob.h>" "glob" "" HAVE_GLOB)

# Xlib check at lines 5330-5331
check_func_headers(xlib "<X11/Xlib.h>;<X11/extensions/Xvlib.h>" "XvGetPortAttribute" "-lXv;-lX11;-lXext" CONFIG_XLIB)

check_header(direct "direct.h" "" HAVE_DIRECT_H)
check_header(dirent "dirent.h" "" HAVE_DIRENT_H)
check_header(dlfcn "dlfcn.h" "" HAVE_DLFCN_H)
check_header(d3d11 "d3d11.h" "" HAVE_D3D11_H)
check_header(dxva "dxva.h" "" HAVE_DXVA_H)
check_header(dxva2api "windows.h;d3d9.h;dxva2api.h" "-D_WIN32_WINNT=0x0600" CONFIG_DXVA2)
check_header(io "io.h" "" HAVE_IO_H)
check_header(mach_time "mach/mach_time.h" "" HAVE_MACH_MACH_TIME_H)
check_header(malloc "malloc.h" "" HAVE_MALLOC_H)
check_header(udplite "net/udplite.h" "" HAVE_UDPLITE_H)
check_header(poll "poll.h" "" HAVE_POLL_H)
check_header(mman "sys/mman.h" "" HAVE_SYS_MMAN_H)
check_header(param "sys/param.h" "" HAVE_SYS_PARAM_H)
check_header(resource "sys/resource.h" "" HAVE_SYS_RESOURCE_H)
check_header(select "sys/select.h" "" HAVE_SYS_SELECT_H)
check_header(time "sys/time.h" "" HAVE_SYS_TIME_H)
check_header(un "sys/un.h" "" HAVE_SYS_UN_H)
check_header(termios "sys/termios.h" "" HAVE_TERMIOS_H)
check_header(unistd "sys/unistd.h" "" HAVE_UNISTD_H)
check_header(valgrind "valgrind/valgrind.h" "" HAVE_VALGRIND_VALGRIND_H)

# Line 5362	# NOTE: on Windows, "shell32.lib" does NOT start with "lib" or "-l", but for my check we need this
check_lib2(commandlinetoargvw "<windows.h>;<shellapi.h>" "CommandLineToArgvW" "-lshell32" HAVE_COMMANDLINETOARGVW)
check_lib2(cryptgenrandom "<windows.h>;<wincrypt.h>" "CryptGenRandom" "-ladvapi32" HAVE_CRYPTGENRANDOM)
check_lib2(getprocessmemoryinfo "<windows.h>;<psapi.h>" "GetProcessMemoryInfo" "-lpsapi" HAVE_GETPROCESSMEMORYINFO)
check_lib(utgetostypefromstring "CoreServices/CoreServices.h" "UTGetOSTypeFromString" "-framework CoreServices" HAVE_UTGETOSTYPEFROMSTRING)

check_struct(ru_maxrss "<sys/time.h>;<sys/resource.h>" "struct rusage" ru_maxrss HAVE_STRUCT_RUSAGE_RU_MAXRSS)

check_cpp_condition(winrt "windows.h" "!WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP)" HAVE_WINRT)

# pthreads vs w32threads - see lines 5382-5389. Note: since we're hardcoding configure 'options', slight change here
if (HAVE_WINDOWS_H)	# Enable w32threads if either of configure script's conditions are met; deal with pthreads later
	check_func_headers(beginthreadex "<windows.h>;<process.h>" "_beginthreadex" "" HAVE_W32THREADS)
	if (NOT HAVE_W32THREADS)
		if (HAVE_WINRT)
			check_func_headers(createthread "<windows.h>" "CreateThread" "" HAVE_W32THREADS)
		endif()
	endif()
endif()

# pthreads - see lines 5391-5414
if (NOT HAVE_W32THREADS AND NOT HAVE_OS2THREADS)
	check_func(pthread_join "pthread_join" HAVE_PTHREAD_JOIN)
	if (ANDROID)	# workaround for older Android omitting pthread_cancel for optimization purposes
		set(HAVE_PTHREAD_CANCEL 1)
	else()
		check_func(pthread_cancel "pthread_cancel" HAVE_PTHREAD_CANCEL)
	endif()
	if (HAVE_PTHREAD_JOIN AND HAVE_PTHREAD_CANCEL)
		set(HAVE_PTHREADS 1)
	endif()
endif()

check_lib2(zlibversion "<zlib.h>" "zlibVersion" "-lz" CONFIG_ZLIB)

# check all listed math_funcs - see lines 5434-5436
set(mathfuncs "atanf;atan2f;cbrt;cbrtf;copysign;cosf;erf;exp2;exp2f;expf;hypot;isfinite;isinf;isnan;ldexpf;llrint;llrintf;log2;log2f;log10f;lrint;lrintf;powf;rint;round;roundf;sinf;trunc;truncf")

foreach(func IN LISTS mathfuncs)
	string(TOUPPER ${func} uppercase_func)
	if (MSVC)
    	check_mathfunc(${func} ${func} "-lmsvcrt" HAVE_${uppercase_func})
	else()
    	check_mathfunc(${func} ${func} "-lm" HAVE_${uppercase_func})
	endif()
endforeach()

# check all listed complex_funcs - see lines 5438-5440
check_complexfunc(cabs cabs HAVE_CABS)
check_complexfunc(cexp cexp HAVE_CEXP)

check_header(sys_videoio "sys/videoio.h" "" HAVE_SYS_VIDEOIO_H)

check_type(ibasefilter "<dshow.h>" "IBaseFilter" CONFIG_DSHOW_INDEV)	# Note: not sure this is the right variable

# FIXME (or don't, as both CONFIG_SNDIO_INDEV and CONFIG_SNDIO_OUTDEV are set to "0"). The simplified check doesn't
# look in /usr/local/include and therefore fails on some targets like FreeBSD, albeit harmlessly
check_header(sndio "sndio.h" "" HAVE_SNDIO_H)
check_struct(audio_buf_info "<sys/soundcard.h>" "audio_buf_info bytes;" "audio_buf_info bytes" HAVE_SYS_SOUNDCARD_H)
if (NOT HAVE_SYS_SOUNDCARD_H)
	check_cc(sys_soundcard "#include <sys/soundcard.h>\naudio_buf_info abc;" HAVE_SYS_SOUNDCARD_H "-D__BSD_VISIBLE;-D__XSI_VISIBLE")
endif()
check_header(soundcard "soundcard.h" "" HAVE_SOUNDCARD_H)

# Xlib: see lines 5718-5719
check_lib2(xopendisplay "<X11/Xlib.h>" "XOpenDisplay" "-lX11" HAVE_XLIB)
if (CONFIG_XLIB)
	set(HAVE_XLIB 1)
endif()

# dxva2api_cobj - see line 5755
check_cc(dxva2api_cobj "#define _WIN32_WINNT 0x0600\n#define COBJMACROS\n#include <windows.h>\n#include <d3d9.h>\n#include <dxva2api.h>\nint main(void) { IDirectXVideoDecoder *o = NULL;\nIDirectXVideoDecoder_Release(o);\nreturn 0; }" HAVE_DXVA2API_COBJ)

# dxva2_lib - see line 6053
if (CONFIG_DXVA2 AND HAVE_DXVA2API_COBJ AND HAVE_COTASKMEMFREE)
	set(HAVE_DXVA2_LIB 1)
endif()

# Threads - see line 6045
if (HAVE_SYNC_VAL_COMPARE_AND_SWAP OR HAVE_ATOMIC_COMPARE_EXCHANGE)
	set(HAVE_ATOMICS_GCC 1)
endif()
if (HAVE_ATOMIC_CAS_PTR OR HAVE_MACHINE_RW_BARRIER)
	set(HAVE_ATOMICS_SUNCC 1)
endif()
if (HAVE_MEMORYBARRIER)
	set(HAVE_ATOMICS_WIN32 1)
endif()
if (HAVE_PTHREADS OR HAVE_OS2THREADS OR HAVE_W32THREADS)
	set(HAVE_ATOMICS_NATIVE 1)
	set(HAVE_THREADS 1)
endif()

# Backup/second test for flt_lim, see lines 4836-4837
check_code(flt_lim cc "<float.h>;<limits.h>" "char c[2 * !!(DBL_MAX == (double)DBL_MAX) - 1]" HAVE_FLT_LIM)

# Run convenience function to set "0" for any variables not set to "1"
set(settings-list "ARCH_AARCH64;ARCH_ALPHA;ARCH_AMD64;ARCH_ARM;ARCH_AVR32;ARCH_AVR32_AP;ARCH_AVR32_UC;ARCH_BFIN;ARCH_IA64;ARCH_M68K;ARCH_MIPS;ARCH_MIPS64;ARCH_PARISC;ARCH_PPC;ARCH_PPC64;ARCH_S390;ARCH_SH4;ARCH_SPARC;ARCH_SPARC64;ARCH_TILEGX;ARCH_TILEPRO;ARCH_TOMI;ARCH_X86;ARCH_X86_32;ARCH_X86_64;HAVE_ARMV5TE;HAVE_ARMV6;HAVE_ARMV6T2;HAVE_ARMV8;HAVE_NEON;HAVE_VFP;HAVE_VFPV3;HAVE_SETEND;HAVE_ALTIVEC;HAVE_DCBZL;HAVE_LDBRX;HAVE_POWER8;HAVE_PPC4XX;HAVE_VSX;HAVE_AESNI;HAVE_AMD3DNOW;HAVE_AMD3DNOWEXT;HAVE_AVX;HAVE_AVX2;HAVE_FMA3;HAVE_FMA4;HAVE_MMX;HAVE_MMXEXT;HAVE_SSE;HAVE_SSE2;HAVE_SSE3;HAVE_SSE4;HAVE_SSE42;HAVE_SSSE3;HAVE_XOP;HAVE_CPUNOP;HAVE_I686;HAVE_MIPSFPU;HAVE_MIPS32R2;HAVE_MIPS32R5;HAVE_MIPS64R2;HAVE_MIPS32R6;HAVE_MIPS64R6;HAVE_MIPSDSP;HAVE_MIPSDSPR2;HAVE_MSA;HAVE_LOONGSON2;HAVE_LOONGSON3;HAVE_MMI;HAVE_ARMV5TE_EXTERNAL;HAVE_ARMV6_EXTERNAL;HAVE_ARMV6T2_EXTERNAL;HAVE_ARMV8_EXTERNAL;HAVE_NEON_EXTERNAL;HAVE_VFP_EXTERNAL;HAVE_VFPV3_EXTERNAL;HAVE_SETEND_EXTERNAL;HAVE_ALTIVEC_EXTERNAL;HAVE_DCBZL_EXTERNAL;HAVE_LDBRX_EXTERNAL;HAVE_POWER8_EXTERNAL;HAVE_PPC4XX_EXTERNAL;HAVE_VSX_EXTERNAL;HAVE_AESNI_EXTERNAL;HAVE_AMD3DNOW_EXTERNAL;HAVE_AMD3DNOWEXT_EXTERNAL;HAVE_AVX_EXTERNAL;HAVE_AVX2_EXTERNAL;HAVE_FMA3_EXTERNAL;HAVE_FMA4_EXTERNAL;HAVE_MMX_EXTERNAL;HAVE_MMXEXT_EXTERNAL;HAVE_SSE_EXTERNAL;HAVE_SSE2_EXTERNAL;HAVE_SSE3_EXTERNAL;HAVE_SSE4_EXTERNAL;HAVE_SSE42_EXTERNAL;HAVE_SSSE3_EXTERNAL;HAVE_XOP_EXTERNAL;HAVE_CPUNOP_EXTERNAL;HAVE_I686_EXTERNAL;HAVE_MIPSFPU_EXTERNAL;HAVE_MIPS32R2_EXTERNAL;HAVE_MIPS32R5_EXTERNAL;HAVE_MIPS64R2_EXTERNAL;HAVE_MIPS32R6_EXTERNAL;HAVE_MIPS64R6_EXTERNAL;HAVE_MIPSDSP_EXTERNAL;HAVE_MIPSDSPR2_EXTERNAL;HAVE_MSA_EXTERNAL;HAVE_LOONGSON2_EXTERNAL;HAVE_LOONGSON3_EXTERNAL;HAVE_MMI_EXTERNAL;HAVE_ARMV5TE_INLINE;HAVE_ARMV6_INLINE;HAVE_ARMV6T2_INLINE;HAVE_ARMV8_INLINE;HAVE_NEON_INLINE;HAVE_VFP_INLINE;HAVE_VFPV3_INLINE;HAVE_SETEND_INLINE;HAVE_ALTIVEC_INLINE;HAVE_DCBZL_INLINE;HAVE_LDBRX_INLINE;HAVE_POWER8_INLINE;HAVE_PPC4XX_INLINE;HAVE_VSX_INLINE;HAVE_AESNI_INLINE;HAVE_AMD3DNOW_INLINE;HAVE_AMD3DNOWEXT_INLINE;HAVE_AVX_INLINE;HAVE_AVX2_INLINE;HAVE_FMA3_INLINE;HAVE_FMA4_INLINE;HAVE_MMX_INLINE;HAVE_MMXEXT_INLINE;HAVE_SSE_INLINE;HAVE_SSE2_INLINE;HAVE_SSE3_INLINE;HAVE_SSE4_INLINE;HAVE_SSE42_INLINE;HAVE_SSSE3_INLINE;HAVE_XOP_INLINE;HAVE_CPUNOP_INLINE;HAVE_I686_INLINE;HAVE_MIPSFPU_INLINE;HAVE_MIPS32R2_INLINE;HAVE_MIPS32R5_INLINE;HAVE_MIPS64R2_INLINE;HAVE_MIPS32R6_INLINE;HAVE_MIPS64R6_INLINE;HAVE_MIPSDSP_INLINE;HAVE_MIPSDSPR2_INLINE;HAVE_MSA_INLINE;HAVE_LOONGSON2_INLINE;HAVE_LOONGSON3_INLINE;HAVE_MMI_INLINE;HAVE_ALIGNED_STACK;HAVE_FAST_64BIT;HAVE_FAST_CLZ;HAVE_FAST_CMOV;HAVE_LOCAL_ALIGNED_8;HAVE_LOCAL_ALIGNED_16;HAVE_LOCAL_ALIGNED_32;HAVE_SIMD_ALIGN_16;HAVE_ATOMICS_GCC;HAVE_ATOMICS_SUNCC;HAVE_ATOMICS_WIN32;HAVE_ATOMIC_CAS_PTR;HAVE_ATOMIC_COMPARE_EXCHANGE;HAVE_MACHINE_RW_BARRIER;HAVE_MEMORYBARRIER;HAVE_MM_EMPTY;HAVE_RDTSC;HAVE_SARESTART;HAVE_SYNC_VAL_COMPARE_AND_SWAP;HAVE_CABS;HAVE_CEXP;HAVE_INLINE_ASM;HAVE_SYMVER;HAVE_YASM;HAVE_BIGENDIAN;HAVE_FAST_UNALIGNED;HAVE_INCOMPATIBLE_LIBAV_ABI;HAVE_ALSA_ASOUNDLIB_H;HAVE_ALTIVEC_H;HAVE_ARPA_INET_H;HAVE_ASM_TYPES_H;HAVE_CDIO_PARANOIA_H;HAVE_CDIO_PARANOIA_PARANOIA_H;HAVE_DEV_BKTR_IOCTL_BT848_H;HAVE_DEV_BKTR_IOCTL_METEOR_H;HAVE_DEV_IC_BT8XX_H;HAVE_DEV_VIDEO_BKTR_IOCTL_BT848_H;HAVE_DEV_VIDEO_METEOR_IOCTL_METEOR_H;HAVE_DIRECT_H;HAVE_DIRENT_H;HAVE_DLFCN_H;HAVE_D3D11_H;HAVE_DXVA_H;HAVE_ES2_GL_H;HAVE_GSM_H;HAVE_IO_H;HAVE_MACH_MACH_TIME_H;HAVE_MACHINE_IOCTL_BT848_H;HAVE_MACHINE_IOCTL_METEOR_H;HAVE_MALLOC_H;HAVE_OPENCV2_CORE_CORE_C_H;HAVE_OPENJPEG_2_1_OPENJPEG_H;HAVE_OPENJPEG_2_0_OPENJPEG_H;HAVE_OPENJPEG_1_5_OPENJPEG_H;HAVE_OPENGL_GL3_H;HAVE_POLL_H;HAVE_SNDIO_H;HAVE_SOUNDCARD_H;HAVE_SYS_MMAN_H;HAVE_SYS_PARAM_H;HAVE_SYS_RESOURCE_H;HAVE_SYS_SELECT_H;HAVE_SYS_SOUNDCARD_H;HAVE_SYS_TIME_H;HAVE_SYS_UN_H;HAVE_SYS_VIDEOIO_H;HAVE_TERMIOS_H;HAVE_UDPLITE_H;HAVE_UNISTD_H;HAVE_VALGRIND_VALGRIND_H;HAVE_WINDOWS_H;HAVE_WINSOCK2_H;HAVE_INTRINSICS_NEON;HAVE_ATANF;HAVE_ATAN2F;HAVE_CBRT;HAVE_CBRTF;HAVE_COPYSIGN;HAVE_COSF;HAVE_ERF;HAVE_EXP2;HAVE_EXP2F;HAVE_EXPF;HAVE_HYPOT;HAVE_ISFINITE;HAVE_ISINF;HAVE_ISNAN;HAVE_LDEXPF;HAVE_LLRINT;HAVE_LLRINTF;HAVE_LOG2;HAVE_LOG2F;HAVE_LOG10F;HAVE_LRINT;HAVE_LRINTF;HAVE_POWF;HAVE_RINT;HAVE_ROUND;HAVE_ROUNDF;HAVE_SINF;HAVE_TRUNC;HAVE_TRUNCF;HAVE_ACCESS;HAVE_ALIGNED_MALLOC;HAVE_ARC4RANDOM;HAVE_CLOCK_GETTIME;HAVE_CLOSESOCKET;HAVE_COMMANDLINETOARGVW;HAVE_COTASKMEMFREE;HAVE_CRYPTGENRANDOM;HAVE_DLOPEN;HAVE_FCNTL;HAVE_FLT_LIM;HAVE_FORK;HAVE_GETADDRINFO;HAVE_GETHRTIME;HAVE_GETOPT;HAVE_GETPROCESSAFFINITYMASK;HAVE_GETPROCESSMEMORYINFO;HAVE_GETPROCESSTIMES;HAVE_GETRUSAGE;HAVE_GETSYSTEMTIMEASFILETIME;HAVE_GETTIMEOFDAY;HAVE_GLOB;HAVE_GLXGETPROCADDRESS;HAVE_GMTIME_R;HAVE_INET_ATON;HAVE_ISATTY;HAVE_JACK_PORT_GET_LATENCY_RANGE;HAVE_KBHIT;HAVE_LOCALTIME_R;HAVE_LSTAT;HAVE_LZO1X_999_COMPRESS;HAVE_MACH_ABSOLUTE_TIME;HAVE_MAPVIEWOFFILE;HAVE_MEMALIGN;HAVE_MKSTEMP;HAVE_MMAP;HAVE_MPROTECT;HAVE_NANOSLEEP;HAVE_PEEKNAMEDPIPE;HAVE_POSIX_MEMALIGN;HAVE_PTHREAD_CANCEL;HAVE_SCHED_GETAFFINITY;HAVE_SETCONSOLETEXTATTRIBUTE;HAVE_SETCONSOLECTRLHANDLER;HAVE_SETMODE;HAVE_SETRLIMIT;HAVE_SLEEP;HAVE_STRERROR_R;HAVE_SYSCONF;HAVE_SYSCTL;HAVE_USLEEP;HAVE_UTGETOSTYPEFROMSTRING;HAVE_VIRTUALALLOC;HAVE_WGLGETPROCADDRESS;HAVE_PTHREADS;HAVE_OS2THREADS;HAVE_W32THREADS;HAVE_AS_DN_DIRECTIVE;HAVE_AS_FUNC;HAVE_AS_OBJECT_ARCH;HAVE_ASM_MOD_Q;HAVE_ATTRIBUTE_MAY_ALIAS;HAVE_ATTRIBUTE_PACKED;HAVE_EBP_AVAILABLE;HAVE_EBX_AVAILABLE;HAVE_GNU_AS;HAVE_GNU_WINDRES;HAVE_IBM_ASM;HAVE_INLINE_ASM_DIRECT_SYMBOL_REFS;HAVE_INLINE_ASM_LABELS;HAVE_INLINE_ASM_NONLOCAL_LABELS;HAVE_PRAGMA_DEPRECATED;HAVE_RSYNC_CONTIMEOUT;HAVE_SYMVER_ASM_LABEL;HAVE_SYMVER_GNU_ASM;HAVE_VFP_ARGS;HAVE_XFORM_ASM;HAVE_XMM_CLOBBERS;HAVE_CONDITION_VARIABLE_PTR;HAVE_SOCKLEN_T;HAVE_STRUCT_ADDRINFO;HAVE_STRUCT_GROUP_SOURCE_REQ;HAVE_STRUCT_IP_MREQ_SOURCE;HAVE_STRUCT_IPV6_MREQ;HAVE_STRUCT_POLLFD;HAVE_STRUCT_RUSAGE_RU_MAXRSS;HAVE_STRUCT_SCTP_EVENT_SUBSCRIBE;HAVE_STRUCT_SOCKADDR_IN6;HAVE_STRUCT_SOCKADDR_SA_LEN;HAVE_STRUCT_SOCKADDR_STORAGE;HAVE_STRUCT_STAT_ST_MTIM_TV_NSEC;HAVE_STRUCT_V4L2_FRMIVALENUM_DISCRETE;HAVE_ATOMICS_NATIVE;HAVE_DOS_PATHS;HAVE_DXVA2_LIB;HAVE_DXVA2API_COBJ;HAVE_LIBC_MSVCRT;HAVE_LIBDC1394_1;HAVE_LIBDC1394_2;HAVE_MAKEINFO;HAVE_MAKEINFO_HTML;HAVE_PERL;HAVE_POD2MAN;HAVE_SDL;HAVE_SECTION_DATA_REL_RO;HAVE_TEXI2HTML;HAVE_THREADS;HAVE_VAAPI_X11;HAVE_VDPAU_X11;HAVE_WINRT;HAVE_XLIB;CONFIG_BSFS;CONFIG_DECODERS;CONFIG_ENCODERS;CONFIG_HWACCELS;CONFIG_PARSERS;CONFIG_INDEVS;CONFIG_OUTDEVS;CONFIG_FILTERS;CONFIG_DEMUXERS;CONFIG_MUXERS;CONFIG_PROTOCOLS;CONFIG_DOC;CONFIG_HTMLPAGES;CONFIG_MANPAGES;CONFIG_PODPAGES;CONFIG_TXTPAGES;CONFIG_AVIO_READING_EXAMPLE;CONFIG_AVIO_DIR_CMD_EXAMPLE;CONFIG_DECODING_ENCODING_EXAMPLE;CONFIG_DEMUXING_DECODING_EXAMPLE;CONFIG_EXTRACT_MVS_EXAMPLE;CONFIG_FILTER_AUDIO_EXAMPLE;CONFIG_FILTERING_AUDIO_EXAMPLE;CONFIG_FILTERING_VIDEO_EXAMPLE;CONFIG_METADATA_EXAMPLE;CONFIG_MUXING_EXAMPLE;CONFIG_QSVDEC_EXAMPLE;CONFIG_REMUXING_EXAMPLE;CONFIG_RESAMPLING_AUDIO_EXAMPLE;CONFIG_SCALING_VIDEO_EXAMPLE;CONFIG_TRANSCODE_AAC_EXAMPLE;CONFIG_TRANSCODING_EXAMPLE;CONFIG_AVISYNTH;CONFIG_BZLIB;CONFIG_CHROMAPRINT;CONFIG_CRYSTALHD;CONFIG_DECKLINK;CONFIG_FREI0R;CONFIG_GCRYPT;CONFIG_GMP;CONFIG_GNUTLS;CONFIG_ICONV;CONFIG_LADSPA;CONFIG_LIBASS;CONFIG_LIBBLURAY;CONFIG_LIBBS2B;CONFIG_LIBCACA;CONFIG_LIBCDIO;CONFIG_LIBCELT;CONFIG_LIBDC1394;CONFIG_LIBDCADEC;CONFIG_LIBFAAC;CONFIG_LIBFDK_AAC;CONFIG_LIBFLITE;CONFIG_LIBFONTCONFIG;CONFIG_LIBFREETYPE;CONFIG_LIBFRIBIDI;CONFIG_LIBGME;CONFIG_LIBGSM;CONFIG_LIBIEC61883;CONFIG_LIBILBC;CONFIG_LIBKVAZAAR;CONFIG_LIBMFX;CONFIG_LIBMODPLUG;CONFIG_LIBMP3LAME;CONFIG_LIBNUT;CONFIG_LIBOPENCORE_AMRNB;CONFIG_LIBOPENCORE_AMRWB;CONFIG_LIBOPENCV;CONFIG_LIBOPENH264;CONFIG_LIBOPENJPEG;CONFIG_LIBOPUS;CONFIG_LIBPULSE;CONFIG_LIBRTMP;CONFIG_LIBRUBBERBAND;CONFIG_LIBSCHROEDINGER;CONFIG_LIBSHINE;CONFIG_LIBSMBCLIENT;CONFIG_LIBSNAPPY;CONFIG_LIBSOXR;CONFIG_LIBSPEEX;CONFIG_LIBSSH;CONFIG_LIBTESSERACT;CONFIG_LIBTHEORA;CONFIG_LIBTWOLAME;CONFIG_LIBUTVIDEO;CONFIG_LIBV4L2;CONFIG_LIBVIDSTAB;CONFIG_LIBVO_AMRWBENC;CONFIG_LIBVORBIS;CONFIG_LIBVPX;CONFIG_LIBWAVPACK;CONFIG_LIBWEBP;CONFIG_LIBX264;CONFIG_LIBX265;CONFIG_LIBXAVS;CONFIG_LIBXCB;CONFIG_LIBXCB_SHM;CONFIG_LIBXCB_SHAPE;CONFIG_LIBXCB_XFIXES;CONFIG_LIBXVID;CONFIG_LIBZIMG;CONFIG_LIBZMQ;CONFIG_LIBZVBI;CONFIG_LZMA;CONFIG_MMAL;CONFIG_NETCDF;CONFIG_NVENC;CONFIG_OPENAL;CONFIG_OPENCL;CONFIG_OPENGL;CONFIG_OPENSSL;CONFIG_SCHANNEL;CONFIG_SDL;CONFIG_SECURETRANSPORT;CONFIG_X11GRAB;CONFIG_XLIB;CONFIG_ZLIB;CONFIG_FTRAPV;CONFIG_GRAY;CONFIG_HARDCODED_TABLES;CONFIG_RUNTIME_CPUDETECT;CONFIG_SAFE_BITSTREAM_READER;CONFIG_SHARED;CONFIG_SMALL;CONFIG_STATIC;CONFIG_SWSCALE_ALPHA;CONFIG_D3D11VA;CONFIG_DXVA2;CONFIG_VAAPI;CONFIG_VDA;CONFIG_VDPAU;CONFIG_XVMC")

foreach(option IN LISTS settings-list)
	set_disabled_to_zero(${option})
endforeach()

# Generate config.h file
configure_file(config.h.in config.h @ONLY)

# Generate avconfig.h file
# Test for BIGENDIAN
if(CMAKE_C_BYTE_ORDER STREQUAL "BIG_ENDIAN")
	set(IS_BIGENDIAN 1)
else()
	set(IS_BIGENDIAN 0)
endif()

file(CONFIGURE
	OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/libavutil/avconfig.h"
	CONTENT
"
/* Generated by cmake for use with ppsspp-ffmpeg module */
#ifndef AVUTIL_AVCONFIG_H
#define AVUTIL_AVCONFIG_H
#define AV_HAVE_BIGENDIAN ${IS_BIGENDIAN}
#define AV_HAVE_FAST_UNALIGNED 0
#define AV_HAVE_INCOMPATIBLE_LIBAV_ABI 0
#endif /* AVUTIL_AVCONFIG_H */
"
	NEWLINE_STYLE LF
)

# Generate ffversion.h header file
# In ffmpeg's version.sh script, 'git' is used, but that's an external dependency we don't otherwise need
# we can just output say the first 7 digits of .git/refs/heads/master, assuming it exists, with hardcoded fallback
set(FILE_PATH "${SRC_DIR}/.git/refs/heads/master")
if(EXISTS ${FILE_PATH})
	file(READ "${FILE_PATH}" GIT_HASH)
	string(SUBSTRING "${GIT_HASH}" 0 7 GIT_HASH)
	set(GIT_HASH \"${GIT_HASH}\")
else()
	set(GIT_HASH \"1e3b496\")
endif()

file(CONFIGURE
	OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/libavutil/ffversion.h"
	CONTENT
"
/* Automatically generated by cmake for use with ppsspp-ffmpeg module */
#ifndef AVUTIL_FFVERSION_H
#define AVUTIL_FFVERSION_H
#define FFMPEG_VERSION ${GIT_HASH}
#endif /* AVUTIL_FFVERSION_H */
"
	NEWLINE_STYLE LF
)
