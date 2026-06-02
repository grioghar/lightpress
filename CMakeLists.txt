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

# bench_ffmpeg is always built but skips internally if ffmpeg is absent
add_executable(bench_ffmpeg tests/bench_ffmpeg.cpp)
target_link_libraries(bench_ffmpeg PRIVATE lightpress)
