cmake_minimum_required(VERSION 3.25)

project(creature-listener
        VERSION "0.1.0"
        DESCRIPTION "Wake word conversational interface for Beaky"
        HOMEPAGE_URL https://github.com/opsnlops/creature-listener
        LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)

# Warning flags for our targets only (not dependencies)
set(CREATURE_LISTENER_WARNING_FLAGS
    -Wshadow
    -Wall
    -Wextra
    -Wpedantic
)

set(PACKAGE_AUTHOR "April White")

find_package(PkgConfig REQUIRED)

# Make our version available to the project
configure_file(src/Version.h.in Version.h)

# Log loudly
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# This project uses threads
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# Force O3 optimization for Release builds
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
        set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
        set(CMAKE_C_FLAGS_RELEASE "-O3 -DNDEBUG")
    endif()
endif()

# --- Dependencies via FetchContent ---

# fmt
message(STATUS "Fetching fmt...")
FetchContent_Declare(
        fmt
        GIT_REPOSITORY https://github.com/fmtlib/fmt
        GIT_TAG 12.1.0
)
FetchContent_MakeAvailable(fmt)
set(FMT_HEADER_ONLY ON)
set(FMT_LOCALE ON)

# spdlog
message(STATUS "Fetching spdlog...")
FetchContent_Declare(
        spdlog
        GIT_REPOSITORY https://github.com/gabime/spdlog
        GIT_TAG v1.16.0
)
FetchContent_MakeAvailable(spdlog)

# argparse
message(STATUS "Fetching argparse...")
FetchContent_Declare(
        argparse
        GIT_REPOSITORY https://github.com/p-ranav/argparse.git
)
FetchContent_MakeAvailable(argparse)

# nlohmann_json
message(STATUS "Fetching nlohmann_json...")
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.11.3
        GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

# whisper.cpp - speech-to-text
message(STATUS "Fetching whisper.cpp...")
set(WHISPER_BUILD_TESTS OFF CACHE BOOL "Disable whisper tests" FORCE)
set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "Disable whisper examples" FORCE)
set(WHISPER_BUILD_SERVER OFF CACHE BOOL "Disable whisper server" FORCE)
set(WHISPER_NO_METAL ON CACHE BOOL "Disable Metal (Pi target)" FORCE)
set(GGML_METAL OFF CACHE BOOL "Disable Metal backend" FORCE)
set(GGML_CUDA OFF CACHE BOOL "Disable CUDA backend" FORCE)
set(GGML_VULKAN OFF CACHE BOOL "Disable Vulkan backend" FORCE)

FetchContent_Declare(
        whisper
        GIT_REPOSITORY https://github.com/ggerganov/whisper.cpp.git
        GIT_TAG v1.7.5
        GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(whisper)

# ONNX Runtime - needed by LOWWI for wake word detection
# Provide it before LOWWI so it finds the TARGET and skips its own platform-hardcoded fetch
message(STATUS "Fetching ONNX Runtime...")
set(ONNXRUNTIME_VERSION "1.18.1")
if(APPLE)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
        set(ONNXRUNTIME_URL "https://github.com/microsoft/onnxruntime/releases/download/v${ONNXRUNTIME_VERSION}/onnxruntime-osx-arm64-${ONNXRUNTIME_VERSION}.tgz")
    else()
        set(ONNXRUNTIME_URL "https://github.com/microsoft/onnxruntime/releases/download/v${ONNXRUNTIME_VERSION}/onnxruntime-osx-x86_64-${ONNXRUNTIME_VERSION}.tgz")
    endif()
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
    set(ONNXRUNTIME_URL "https://github.com/microsoft/onnxruntime/releases/download/v${ONNXRUNTIME_VERSION}/onnxruntime-linux-aarch64-${ONNXRUNTIME_VERSION}.tgz")
else()
    set(ONNXRUNTIME_URL "https://github.com/microsoft/onnxruntime/releases/download/v${ONNXRUNTIME_VERSION}/onnxruntime-linux-x64-${ONNXRUNTIME_VERSION}.tgz")
endif()

FetchContent_Declare(
        onnxruntime
        URL ${ONNXRUNTIME_URL}
)
FetchContent_MakeAvailable(onnxruntime)

# Create an onnxruntime target that LOWWI will find
add_library(onnxruntime INTERFACE)
target_include_directories(onnxruntime INTERFACE ${onnxruntime_SOURCE_DIR}/include)
if(APPLE)
    target_link_libraries(onnxruntime INTERFACE "${onnxruntime_SOURCE_DIR}/lib/libonnxruntime.dylib")
else()
    target_link_libraries(onnxruntime INTERFACE "${onnxruntime_SOURCE_DIR}/lib/libonnxruntime.so")
endif()

# LOWWI - wake word detection (openWakeWord ONNX models)
message(STATUS "Fetching LOWWI...")
set(CLFML_LOWWI_BUILD_EXAMPLE_PROJECTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
        lowwi
        GIT_REPOSITORY https://github.com/CLFML/lowwi.git
        GIT_TAG main
        GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(lowwi)

# PortAudio - audio capture
message(STATUS "Fetching PortAudio...")
set(PA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(PA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
        portaudio
        GIT_REPOSITORY https://github.com/PortAudio/portaudio.git
        GIT_TAG v19.7.0
        GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(portaudio)

# --- System dependencies ---

# libcurl for HTTP client
find_package(CURL REQUIRED)

# --- Data files ---
set(CREATURE_DATA_DIR "${CMAKE_BINARY_DIR}/data")
file(MAKE_DIRECTORY ${CREATURE_DATA_DIR})

# Whisper tiny.en model (~75MB) - English-only, fast on Pi 5
set(WHISPER_MODEL_FILE "${CREATURE_DATA_DIR}/ggml-tiny.en.bin")
if(NOT EXISTS ${WHISPER_MODEL_FILE})
    message(STATUS "Downloading whisper tiny.en model (~75MB)...")
    file(DOWNLOAD
        "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-tiny.en.bin"
        ${WHISPER_MODEL_FILE}
        SHOW_PROGRESS
        STATUS WHISPER_DL_STATUS
    )
    list(GET WHISPER_DL_STATUS 0 WHISPER_DL_CODE)
    if(NOT WHISPER_DL_CODE EQUAL 0)
        message(WARNING "Failed to download whisper model: ${WHISPER_DL_STATUS}")
    else()
        message(STATUS "Whisper model downloaded to ${WHISPER_MODEL_FILE}")
    endif()
else()
    message(STATUS "Whisper model already present: ${WHISPER_MODEL_FILE}")
endif()

# Silero VAD model (~2MB)
set(SILERO_VAD_MODEL "${CREATURE_DATA_DIR}/silero_vad.onnx")
if(NOT EXISTS ${SILERO_VAD_MODEL})
    message(STATUS "Downloading Silero VAD model...")
    file(DOWNLOAD
        "https://github.com/snakers4/silero-vad/raw/master/src/silero_vad/data/silero_vad.onnx"
        ${SILERO_VAD_MODEL}
        SHOW_PROGRESS
        STATUS SILERO_DL_STATUS
    )
    list(GET SILERO_DL_STATUS 0 SILERO_DL_CODE)
    if(NOT SILERO_DL_CODE EQUAL 0)
        message(WARNING "Failed to download Silero VAD model: ${SILERO_DL_STATUS}")
    else()
        message(STATUS "Silero VAD model downloaded to ${SILERO_VAD_MODEL}")
    endif()
else()
    message(STATUS "Silero VAD model already present: ${SILERO_VAD_MODEL}")
endif()

# Install data files for packaging
set(CREATURE_INSTALL_DATA_DIR "/usr/share/creature-listener/data")
install(FILES ${WHISPER_MODEL_FILE}
        DESTINATION ${CREATURE_INSTALL_DATA_DIR}
        COMPONENT creature-listener
)
install(FILES ${SILERO_VAD_MODEL}
        DESTINATION ${CREATURE_INSTALL_DATA_DIR}
        COMPONENT creature-listener
)

# --- Source files ---

set(LISTENER_SOURCES
    src/main.cpp
    src/config/Configuration.h
    src/config/CommandLine.h
    src/config/CommandLine.cpp
    src/audio/AudioCapture.h
    src/audio/AudioCapture.cpp
    src/audio/VoiceActivityDetector.h
    src/audio/VoiceActivityDetector.cpp
    src/wakeword/WakeWordDetector.h
    src/wakeword/WakeWordDetector.cpp
    src/stt/SpeechToText.h
    src/stt/SpeechToText.cpp
    src/llm/LLMClient.h
    src/llm/LLMClient.cpp
    src/llm/ConversationHistory.h
    src/llm/ConversationHistory.cpp
    src/server/CreatureServerClient.h
    src/server/CreatureServerClient.cpp
    src/util/namespace-stuffs.h
)

add_executable(creature-listener ${LISTENER_SOURCES})

target_compile_options(creature-listener PRIVATE ${CREATURE_LISTENER_WARNING_FLAGS})

target_include_directories(creature-listener PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}  # For Version.h
)

target_link_libraries(creature-listener PRIVATE
    fmt::fmt
    spdlog::spdlog
    argparse
    nlohmann_json::nlohmann_json
    whisper
    CLFML::lowwi
    portaudio
    CURL::libcurl
    Threads::Threads
)

# --- Install ---
install(TARGETS creature-listener
        RUNTIME DESTINATION bin
)
