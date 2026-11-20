# CMake toolchain for building multitap frontends (piccolo, piccoletto) for
# Android (arm64-v8a, or armeabi-v7a: MULTITAP_ANDROID_ABI) with the distro's clang instead of the NDK's. Each
# frontend's android/build-apk.sh passes it as CMAKE_TOOLCHAIN_FILE.
#
# Why: Google only ships the NDK's compiler for x86_64 hosts, and the main
# development machine is Linux arm64. From the NDK we only use files that are
# the same on every host: the sysroot (Bionic headers and libraries, libc++)
# and compiler-rt's builtins library. Any recent clang can compile for
# aarch64-linux-android with those.
#
# Variables (pass with -D, or set in the environment):
#   ANDROID_NDK_ROOT     the unpacked NDK (default ~/Android/ndk/android-ndk-r30)
#   MULTITAP_LLVM_ROOT   the LLVM install with clang + lld (default /usr/lib/llvm-21). On an
#                        x86_64 machine (CI) the NDK's own one works too:
#                        $ANDROID_NDK_ROOT/toolchains/llvm/prebuilt/linux-x86_64
#   MULTITAP_ANDROID_API minimum Android API level (default 28, Android 9)

if(NOT ANDROID_NDK_ROOT)
    if(DEFINED ENV{ANDROID_NDK_ROOT})
        set(ANDROID_NDK_ROOT "$ENV{ANDROID_NDK_ROOT}")
    else()
        set(ANDROID_NDK_ROOT "$ENV{HOME}/Android/ndk/android-ndk-r30")
    endif()
endif()
if(NOT MULTITAP_LLVM_ROOT)
    if(DEFINED ENV{MULTITAP_LLVM_ROOT})
        set(MULTITAP_LLVM_ROOT "$ENV{MULTITAP_LLVM_ROOT}")
    else()
        set(MULTITAP_LLVM_ROOT "/usr/lib/llvm-21")
    endif()
endif()
if(NOT MULTITAP_ANDROID_API)
    # 28 (Android 9): its libvulkan.so exports the Vulkan 1.1 functions
    # vk_context calls (vkEnumerateInstanceVersion, vkGetPhysicalDeviceFeatures2).
    # The Shield TV is on 30.
    set(MULTITAP_ANDROID_API 28)
endif()

# Try-compile projects run this file again in their own folder: hand the
# variables down.
# The ABI: arm64-v8a (default), or armeabi-v7a for 32-bit devices such as the
# SHIELD Portable (Tegra 4, Android 5.1): that build has no Vulkan
# (MULTITAP_GPU_VULKAN=OFF) and goes with MULTITAP_ANDROID_API 21.
if(NOT MULTITAP_ANDROID_ABI)
    set(MULTITAP_ANDROID_ABI arm64-v8a)
endif()
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES ANDROID_NDK_ROOT MULTITAP_LLVM_ROOT MULTITAP_ANDROID_API
     MULTITAP_ANDROID_ABI)

# The NDK keeps the sysroot inside the (x86_64) prebuilt toolchain folder.
# Only the files are used, never the binaries next to them.
set(_ndk_prebuilt "${ANDROID_NDK_ROOT}/toolchains/llvm/prebuilt/linux-x86_64")
if(NOT EXISTS "${_ndk_prebuilt}/sysroot/usr/include/android/api-level.h")
    message(FATAL_ERROR "No Android NDK sysroot under ${_ndk_prebuilt}. Set ANDROID_NDK_ROOT.")
endif()
if(NOT EXISTS "${MULTITAP_LLVM_ROOT}/bin/clang")
    message(FATAL_ERROR "No clang in ${MULTITAP_LLVM_ROOT}/bin. Set MULTITAP_LLVM_ROOT.")
endif()

# Plain "Linux" would make CMake and SDL treat this as desktop Linux. "Android"
# would make CMake look for the NDK's own (x86_64) compiler. So: CMake's
# generic Android support is skipped and we say ourselves what Android means.
set(CMAKE_SYSTEM_NAME Linux)
set(ANDROID TRUE)
if(MULTITAP_ANDROID_ABI STREQUAL "armeabi-v7a")
    # The NDK's ARMv7 target: Thumb-2, VFPv3-D16, soft-float calls (NEON is
    # on every device Android 5 runs on, but the compiler doesn't assume it).
    set(CMAKE_SYSTEM_PROCESSOR armv7-a)
    set(CMAKE_ANDROID_ARCH_ABI armeabi-v7a)
    set(_target "armv7a-linux-androideabi${MULTITAP_ANDROID_API}")
    set(_library_architecture "arm-linux-androideabi")
elseif(MULTITAP_ANDROID_ABI STREQUAL "arm64-v8a")
    set(CMAKE_SYSTEM_PROCESSOR aarch64)
    set(CMAKE_ANDROID_ARCH_ABI arm64-v8a)
    set(_target "aarch64-linux-android${MULTITAP_ANDROID_API}")
    set(_library_architecture "aarch64-linux-android")
else()
    message(FATAL_ERROR "MULTITAP_ANDROID_ABI must be arm64-v8a or armeabi-v7a, not ${MULTITAP_ANDROID_ABI}")
endif()

set(CMAKE_C_COMPILER   "${MULTITAP_LLVM_ROOT}/bin/clang")
set(CMAKE_CXX_COMPILER "${MULTITAP_LLVM_ROOT}/bin/clang++")
set(CMAKE_ASM_COMPILER "${MULTITAP_LLVM_ROOT}/bin/clang")
set(CMAKE_C_COMPILER_TARGET   "${_target}")
set(CMAKE_CXX_COMPILER_TARGET "${_target}")
set(CMAKE_ASM_COMPILER_TARGET "${_target}")
set(CMAKE_AR     "${MULTITAP_LLVM_ROOT}/bin/llvm-ar"     CACHE FILEPATH "")
set(CMAKE_RANLIB "${MULTITAP_LLVM_ROOT}/bin/llvm-ranlib" CACHE FILEPATH "")
set(CMAKE_STRIP  "${MULTITAP_LLVM_ROOT}/bin/llvm-strip"  CACHE FILEPATH "")

set(CMAKE_SYSROOT "${_ndk_prebuilt}/sysroot")

# clang links compiler-rt's builtins and libunwind from its "resource
# directory", and the distro's clang has no Android build of them. So we make
# a resource directory of our own: the distro clang's headers (they must match
# the compiler) plus the NDK's Android runtime libraries. Both are links,
# nothing is copied.
file(GLOB _ndk_clang_dirs "${_ndk_prebuilt}/lib/clang/*")
list(GET _ndk_clang_dirs 0 _ndk_clang_dir)
file(GLOB _llvm_clang_dirs "${MULTITAP_LLVM_ROOT}/lib/clang/*")
list(GET _llvm_clang_dirs 0 _llvm_clang_dir)
set(_resource_dir "${CMAKE_BINARY_DIR}/clang-resource")
file(MAKE_DIRECTORY "${_resource_dir}")
if(NOT IS_SYMLINK "${_resource_dir}/include")
    file(CREATE_LINK "${_llvm_clang_dir}/include" "${_resource_dir}/include" SYMBOLIC)
endif()
if(NOT IS_SYMLINK "${_resource_dir}/lib")
    file(CREATE_LINK "${_ndk_clang_dir}/lib" "${_resource_dir}/lib" SYMBOLIC)
endif()

# Flags the NDK's own toolchain file uses for arm64, minus the ones newer clang
# already defaults to for Android.
set(_common_flags "-resource-dir=${_resource_dir} -fPIC -ffunction-sections -fdata-sections")
set(CMAKE_C_FLAGS_INIT   "${_common_flags}")
set(CMAKE_CXX_FLAGS_INIT "${_common_flags}")
set(CMAKE_ASM_FLAGS_INIT "${_common_flags}")

# Link with lld, compiler-rt and libc++ as a static library (one .so per app,
# nothing else to ship). Bionic needs 16 KB page alignment on new devices.
set(_link_flags "-fuse-ld=lld -rtlib=compiler-rt -unwindlib=libunwind -static-libstdc++ -Wl,--build-id=sha1 -Wl,--no-undefined -Wl,-z,max-page-size=16384 -Wl,--gc-sections")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${_link_flags}")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${_link_flags}")
set(CMAKE_EXE_LINKER_FLAGS_INIT    "${_link_flags}")

# Only look for libraries and headers in the sysroot, programs on the host.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
# Bionic's per-API libraries (libandroid.so, libvulkan.so, ...) live in
# usr/lib/<triple>/<api>.
set(CMAKE_LIBRARY_ARCHITECTURE "${_library_architecture}")
set(CMAKE_SYSTEM_LIBRARY_PATH "/usr/lib/${_library_architecture}/${MULTITAP_ANDROID_API}")
