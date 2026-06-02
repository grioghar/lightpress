cmake_minimum_required(VERSION 3.16)
project(lightpress LANGUAGES CXX VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/W3 /wd4244 /wd4267 /wd4305)
else()
  add_compile_options(-Wall -Wextra -O2 -Wno-unused-parameter)
endif()

add_library(lightpress STATIC
  src/jpeg_enc.cpp
  src/png_enc.cpp
  src/exif.cpp
  src/resize.cpp
  src/container.cpp
  src/image_io.cpp
  src/ffmpeg.cpp
)
target_include_directories(lightpress PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Link math library on Linux/macOS
if(NOT MSVC AND NOT WIN32)
  target_link_libraries(lightpress PUBLIC m)
endif()

enable_testing()

foreach(t jpeg png exif resize container)
  add_executable(test_${t} tests/test_${t}.cpp)
  target_link_libraries(test_${t} PRIVATE lightpress)
  # Tests must run with assertions enabled regardless of build type
  if(MSVC)
    target_compile_options(test_${t} PRIVATE /UNDEBUG)
  else()
    target_compile_options(test_${t} PRIVATE -UNDEBUG)
  endif()
  add_test(NAME ${t} COMMAND test_${t})
endforeach()

# bench_ffmpeg is always built but skips internally if ffmpeg is absent on PATH
add_executable(bench_ffmpeg tests/bench_ffmpeg.cpp)
target_link_libraries(bench_ffmpeg PRIVATE lightpress)

# ── grioghar/ffmpeg integration ───────────────────────────────────────────
# lightpress uses ffmpeg as an OPTIONAL subprocess for operations outside
# its own codec scope: video transcoding (H.264→H.265), audio conversion,
# and format detection.  Three discovery modes, tried in order:
#
#   1. LIGHTPRESS_FFMPEG_BINARY — explicit path supplied by the caller.
#   2. System ffmpeg on PATH (find_program).
#   3. vendor/ffmpeg submodule — builds ffmpeg from source using its own
#      configure script (requires nasm/yasm and a POSIX shell; skipped on
#      Windows unless WSL2 is available).
#
# In all cases, lightpress itself has zero link-time dependency on ffmpeg;
# the binary path is embedded as a compile-time string and invoked via
# std::system() / popen() at runtime.  Set LIGHTPRESS_FFMPEG_BINARY="" to
# disable the subprocess integration entirely.

option(LIGHTPRESS_USE_VENDORED_FFMPEG
       "Build the grioghar/ffmpeg submodule and use it as the subprocess"
       OFF)

if(LIGHTPRESS_FFMPEG_BINARY)
  message(STATUS "lightpress: using explicit ffmpeg at ${LIGHTPRESS_FFMPEG_BINARY}")
  set(LP_FFMPEG_PATH "${LIGHTPRESS_FFMPEG_BINARY}")
elseif(LIGHTPRESS_USE_VENDORED_FFMPEG AND EXISTS
       "${CMAKE_CURRENT_SOURCE_DIR}/vendor/ffmpeg/configure")
  if(NOT WIN32)
    # Build a minimal, LGPL-only ffmpeg (no GPL codecs) from the submodule.
    set(LP_FFMPEG_SRC "${CMAKE_CURRENT_SOURCE_DIR}/vendor/ffmpeg")
    set(LP_FFMPEG_BUILD "${CMAKE_CURRENT_BINARY_DIR}/ffmpeg-build")
    set(LP_FFMPEG_INSTALL "${CMAKE_CURRENT_BINARY_DIR}/ffmpeg-install")
    file(MAKE_DIRECTORY "${LP_FFMPEG_BUILD}")
    # Configure is run at CMake time so the binary is ready for tests.
    execute_process(
      COMMAND "${LP_FFMPEG_SRC}/configure"
              --prefix=${LP_FFMPEG_INSTALL}
              --disable-doc
              --disable-debug
              --disable-ffplay
              --enable-small
              # LGPL-only profile: no GPL encoders
              --disable-gpl
              --enable-avcodec
              --enable-avformat
              --enable-swscale
              --enable-swresample
      WORKING_DIRECTORY "${LP_FFMPEG_BUILD}"
      RESULT_VARIABLE _ff_cfg
      OUTPUT_QUIET ERROR_QUIET)
    if(_ff_cfg EQUAL 0)
      add_custom_target(ffmpeg-build
        COMMAND make -j${CMAKE_BUILD_PARALLEL_LEVEL} install
        WORKING_DIRECTORY "${LP_FFMPEG_BUILD}"
        COMMENT "Building grioghar/ffmpeg (LGPL profile)")
      set(LP_FFMPEG_PATH "${LP_FFMPEG_INSTALL}/bin/ffmpeg")
      message(STATUS "lightpress: vendored ffmpeg will build to ${LP_FFMPEG_PATH}")
    else()
      message(WARNING "lightpress: vendored ffmpeg configure failed — falling back to system ffmpeg")
    endif()
  else()
    message(STATUS "lightpress: vendored ffmpeg build skipped on Windows; using system ffmpeg")
  endif()
endif()

if(NOT LP_FFMPEG_PATH)
  find_program(LP_FFMPEG_PATH ffmpeg DOC "ffmpeg binary for lightpress subprocess")
  if(LP_FFMPEG_PATH)
    message(STATUS "lightpress: found system ffmpeg at ${LP_FFMPEG_PATH}")
  else()
    message(STATUS "lightpress: ffmpeg not found — transcoding features disabled at runtime")
    set(LP_FFMPEG_PATH "")
  endif()
endif()

# Embed the resolved path as a compile-time string so lp::ffmpeg_path() returns it.
target_compile_definitions(lightpress PUBLIC
  LP_FFMPEG_PATH="${LP_FFMPEG_PATH}")
