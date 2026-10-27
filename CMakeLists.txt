
cmake_minimum_required(VERSION 3.19)

project(KROBUS
    VERSION 1.0.0
    LANGUAGES CXX RC
)

# ============================================
# 1. C++ AND QT CONFIGURATION
# ============================================

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

include(GNUInstallDirs)

find_package(Qt6 REQUIRED COMPONENTS
    Core
    Widgets
    Network
    Multimedia
    WebSockets
)

qt_standard_project_setup()

# ============================================
# 2. KROBUS SOURCE FILES
# ============================================

qt_add_executable(KROBUS
    WIN32
    MACOSX_BUNDLE

    main.cpp
    mainwindow.cpp
    mainwindow.h
    mainwindow.ui

    wakeworddetector.cpp
    wakeworddetector.h

    krobus.qrc
)


# ============================================
# KROBUS WINDOWS APPLICATION ICON
# ============================================

if(WIN32)
    target_sources(KROBUS PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/krobus_icon.rc"
    )
endif()


# ============================================
# 3. QT LIBRARIES
# ============================================

target_link_libraries(KROBUS
    PRIVATE
        Qt6::Core
        Qt6::Widgets
        Qt6::Network
        Qt6::Multimedia
        Qt6::WebSockets
)

# ============================================
# 4. SHERPA-ONNX SDK LOCATION
# ============================================

set(SHERPA_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/sherpa-onnx"
)

set(SHERPA_INCLUDE_DIR
    "${SHERPA_DIR}/include"
)

set(SHERPA_LIBRARY_DIR
    "${SHERPA_DIR}/lib"
)

set(SHERPA_HEADER
    "${SHERPA_INCLUDE_DIR}/sherpa-onnx/c-api/c-api.h"
)

# Confirm the C API header exists.
if(NOT EXISTS "${SHERPA_HEADER}")
    message(FATAL_ERROR
        "KROBUS: Sherpa-ONNX C API header not found: "
        "${SHERPA_HEADER}"
    )
endif()

# Add the official C API header path.
target_include_directories(KROBUS
    PRIVATE
        "${SHERPA_INCLUDE_DIR}"
)

message(STATUS
    "KROBUS: Sherpa-ONNX C API header found."
)

# ============================================
# 5. SHERPA-ONNX DLL FILES
# ============================================

# The Windows runtime DLLs are in the lib folder.
# We dynamically load sherpa-onnx-c-api.dll
# with QLibrary in wakeworddetector.cpp.
#
# We do not link directly to Microsoft's
# Sherpa-ONNX import libraries from MinGW.

set(SHERPA_C_API_DLL
    "${SHERPA_LIBRARY_DIR}/sherpa-onnx-c-api.dll"
)

set(ONNX_RUNTIME_DLL
    "${SHERPA_LIBRARY_DIR}/onnxruntime.dll"
)

if(NOT EXISTS "${SHERPA_C_API_DLL}")
    message(FATAL_ERROR
        "KROBUS: sherpa-onnx-c-api.dll not found."
    )
endif()

if(NOT EXISTS "${ONNX_RUNTIME_DLL}")
    message(FATAL_ERROR
        "KROBUS: onnxruntime.dll not found."
    )
endif()

file(GLOB SHERPA_DLLS
    CONFIGURE_DEPENDS
    "${SHERPA_LIBRARY_DIR}/*.dll"
)

# Copy required DLL files beside KROBUS.exe
# automatically whenever KROBUS is built.

add_custom_command(
    TARGET KROBUS
    POST_BUILD

    COMMAND "${CMAKE_COMMAND}" -E
        copy_if_different
        ${SHERPA_DLLS}
        "$<TARGET_FILE_DIR:KROBUS>"

    COMMENT
        "Copying Sherpa-ONNX runtime DLLs"

    VERBATIM
)

message(STATUS
    "KROBUS: Sherpa-ONNX DLL deployment enabled."
)

# ============================================
# 6. WAKE-WORD MODEL
# ============================================

set(KROBUS_WAKEWORD_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/wakeword/sherpa-onnx-kws-zipformer-gigaspeech-3.3M-2024-01-01"
)

set(KROBUS_KEYWORDS_FILE
    "${KROBUS_WAKEWORD_DIR}/keywords_krobus.txt"
)

set(KROBUS_TOKENS_FILE
    "${KROBUS_WAKEWORD_DIR}/tokens.txt"
)

# Verify the required model configuration.
if(NOT EXISTS "${KROBUS_KEYWORDS_FILE}")
    message(FATAL_ERROR
        "KROBUS: keywords_krobus.txt not found."
    )
endif()

if(NOT EXISTS "${KROBUS_TOKENS_FILE}")
    message(FATAL_ERROR
        "KROBUS: tokens.txt not found."
    )
endif()

# Copy the entire model directory into
# a folder named wakeword beside KROBUS.exe.
#
# This matches the model paths in
# wakeworddetector.cpp.

add_custom_command(
    TARGET KROBUS
    POST_BUILD

    COMMAND "${CMAKE_COMMAND}" -E
        copy_directory
        "${KROBUS_WAKEWORD_DIR}"
        "$<TARGET_FILE_DIR:KROBUS>/wakeword"

    COMMENT
        "Copying Hey Krobus wake-word model"

    VERBATIM
)

message(STATUS
    "KROBUS: Hey Krobus model found."
)

# ============================================
# 7. INSTALL KROBUS
# ============================================

install(
    TARGETS KROBUS

    BUNDLE DESTINATION .

    RUNTIME DESTINATION
        "${CMAKE_INSTALL_BINDIR}"

    LIBRARY DESTINATION
        "${CMAKE_INSTALL_LIBDIR}"
)

# Install the Sherpa DLL dependencies.
install(
    FILES ${SHERPA_DLLS}

    DESTINATION
        "${CMAKE_INSTALL_BINDIR}"
)

# Install the wake-word model.
install(
    DIRECTORY "${KROBUS_WAKEWORD_DIR}/"

    DESTINATION
        "${CMAKE_INSTALL_BINDIR}/wakeword"
)

# ============================================
# 8. QT APPLICATION DEPLOYMENT
# ============================================

qt_generate_deploy_app_script(
    TARGET KROBUS
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)

install(SCRIPT ${deploy_script})


# Windows global keyboard shortcut support
if(WIN32)
    target_link_libraries(KROBUS PRIVATE user32)
endif()


# ============================================
# KROBUS APPEARANCE PACK
# ============================================

set(KROBUS_APPEARANCES
    "${CMAKE_CURRENT_SOURCE_DIR}/appearances/sprites"
)

if(NOT EXISTS
    "${KROBUS_APPEARANCES}/classic/none/idle.png"
)
    message(FATAL_ERROR
        "KROBUS appearance pack was not found."
    )
endif()

# Copy appearance sprites beside KROBUS.exe
add_custom_command(
    TARGET KROBUS
    POST_BUILD

    COMMAND "${CMAKE_COMMAND}" -E copy_directory
        "${KROBUS_APPEARANCES}"
        "$<TARGET_FILE_DIR:KROBUS>/appearances/sprites"

    COMMENT "Copying KROBUS appearance sprites"
    VERBATIM
)

# Include assets in the future installer
install(
    DIRECTORY "${KROBUS_APPEARANCES}/"
    DESTINATION
        "${CMAKE_INSTALL_BINDIR}/appearances/sprites"
)

