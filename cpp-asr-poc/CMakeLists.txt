cmake_minimum_required(VERSION 3.14)
project(cpp_asr_poc CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra)

# 코어 라이브러리 (외부 의존성 없음): PoC-A 전단 + PoC-B 보조 로직 + 운영화 골격
add_library(asr_poc STATIC
  src/g711.cpp
  src/wav_io.cpp
  src/vad.cpp
  src/resample.cpp
  src/consent_match.cpp
  src/ring_buffer.cpp
  src/frame_io.cpp
  src/uds_server.cpp
  src/hybrid.cpp
  src/fir.cpp
  src/streaming_resampler.cpp
  src/uds_client.cpp
  src/pcmu_sender.cpp
)
target_include_directories(asr_poc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
find_package(Threads REQUIRED)
target_link_libraries(asr_poc PUBLIC Threads::Threads)

# 세그멘터 CLI (PoC-A)
add_executable(asr_segmenter src/main.cpp)
target_link_libraries(asr_segmenter PRIVATE asr_poc)

# 합성 테스트 오디오 생성기
add_executable(gen_test_audio tools/gen_test_audio.cpp)
target_link_libraries(gen_test_audio PRIVATE asr_poc)

# 송신측 데모 CLI (메인 백엔드 → UDS). 모델 불필요.
add_executable(asr_feed src/main_feed.cpp)
target_link_libraries(asr_feed PRIVATE asr_poc)

# 자동 테스트
enable_testing()
add_executable(test_vad tests/test_vad.cpp)
target_link_libraries(test_vad PRIVATE asr_poc)
add_test(NAME vad_segmenter COMMAND test_vad)

add_executable(test_resample tests/test_resample.cpp)
target_link_libraries(test_resample PRIVATE asr_poc)
add_test(NAME resample_and_match COMMAND test_resample)

add_executable(test_ipc tests/test_ipc.cpp)
target_link_libraries(test_ipc PRIVATE asr_poc)
add_test(NAME ipc_framing_uds COMMAND test_ipc)

add_executable(test_hybrid tests/test_hybrid.cpp)
target_link_libraries(test_hybrid PRIVATE asr_poc)
add_test(NAME hybrid_orchestration COMMAND test_hybrid)

add_executable(test_sender tests/test_sender.cpp)
target_link_libraries(test_sender PRIVATE asr_poc)
add_test(NAME pcmu_sender COMMAND test_sender)

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

# ---- PoC-C: whisper.cpp 보조 전사 (소스가 있을 때만 빌드) ----
# git clone https://github.com/ggml-org/whisper.cpp third_party/whisper.cpp
set(WHISPER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/whisper.cpp)
if(EXISTS ${WHISPER_DIR}/CMakeLists.txt)
  message(STATUS "whisper.cpp found -> building PoC-C (asr_transcribe)")
  set(WHISPER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(WHISPER_BUILD_SERVER OFF CACHE BOOL "" FORCE)
  add_subdirectory(${WHISPER_DIR} whisper_build)
  add_executable(asr_transcribe src/main_transcribe.cpp src/asr_whisper.cpp)
  target_include_directories(asr_transcribe PRIVATE ${WHISPER_DIR}/include)
  target_link_libraries(asr_transcribe PRIVATE asr_poc whisper)
else()
  message(WARNING "whisper.cpp not found in ${WHISPER_DIR} -> skipping PoC-C (asr_transcribe). "
                  "See README: clone whisper.cpp there.")
endif()

# ---- 운영화 결선: asr_pipeline (Vosk + whisper 둘 다 있을 때만) ----
if(TARGET asr_consent AND TARGET asr_transcribe)
  message(STATUS "Vosk + whisper available -> building asr_pipeline (UDS hybrid)")
  add_executable(asr_pipeline src/main_pipeline.cpp src/asr_vosk.cpp src/asr_whisper.cpp)
  target_include_directories(asr_pipeline PRIVATE ${VOSK_DIR} ${WHISPER_DIR}/include)
  target_link_libraries(asr_pipeline PRIVATE asr_poc ${VOSK_LIB} whisper)
  set_target_properties(asr_pipeline PROPERTIES
    BUILD_RPATH ${VOSK_DIR}
    INSTALL_RPATH ${VOSK_DIR})
endif()
