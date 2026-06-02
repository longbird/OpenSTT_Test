cmake_minimum_required(VERSION 3.14)
project(cpp_asr_poc CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra)

# 코어 라이브러리 (외부 의존성 없음): PoC-A 전단 + PoC-B 보조 로직
add_library(asr_poc STATIC
  src/g711.cpp
  src/wav_io.cpp
  src/vad.cpp
  src/resample.cpp
  src/consent_match.cpp
)
target_include_directories(asr_poc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# 세그멘터 CLI (PoC-A)
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

add_executable(test_resample tests/test_resample.cpp)
target_link_libraries(test_resample PRIVATE asr_poc)
add_test(NAME resample_and_match COMMAND test_resample)

# ---- PoC-B: Vosk 동의 감지 (libvosk 가 있을 때만 빌드) ----
# 라이브러리 위치: third_party/vosk/{libvosk.so, vosk_api.h}
set(VOSK_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/vosk)
find_library(VOSK_LIB NAMES vosk PATHS ${VOSK_DIR} NO_DEFAULT_PATH)
if(VOSK_LIB AND EXISTS ${VOSK_DIR}/vosk_api.h)
  message(STATUS "libvosk found: ${VOSK_LIB} -> building PoC-B (asr_consent)")
  add_executable(asr_consent src/main_consent.cpp src/asr_vosk.cpp)
  target_include_directories(asr_consent PRIVATE ${VOSK_DIR})
  target_link_libraries(asr_consent PRIVATE asr_poc ${VOSK_LIB})
  set_target_properties(asr_consent PROPERTIES
    BUILD_RPATH ${VOSK_DIR}
    INSTALL_RPATH ${VOSK_DIR})
else()
  message(WARNING "libvosk not found in ${VOSK_DIR} -> skipping PoC-B (asr_consent). "
                  "See README: place libvosk.so + vosk_api.h there.")
endif()
