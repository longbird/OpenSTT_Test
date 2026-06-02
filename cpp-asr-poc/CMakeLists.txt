cmake_minimum_required(VERSION 3.14)
project(cpp_asr_poc CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra)

# PoC-A 코어 라이브러리 (외부 의존성 없음)
add_library(asr_poc STATIC
  src/g711.cpp
  src/wav_io.cpp
  src/vad.cpp
)
target_include_directories(asr_poc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# 세그멘터 CLI
add_executable(asr_segmenter src/main.cpp)
target_link_libraries(asr_segmenter PRIVATE asr_poc)

# 합성 테스트 오디오 생성기
add_executable(gen_test_audio tools/gen_test_audio.cpp)
target_link_libraries(gen_test_audio PRIVATE asr_poc)

# 자동 테스트
enable_testing()
add_executable(test_vad tests/test_vad.cpp)
target_link_libraries(test_vad PRIVATE asr_poc)
add_test(NAME vad_segmenter COMMAND test_vad)
