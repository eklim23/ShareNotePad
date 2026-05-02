cmake_minimum_required(VERSION 3.20)

if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()

project(ShareNotepad LANGUAGES CXX)

option(SHARENOTEPAD_USE_WEBVIEW2_UI "Use the WebView2 based ShareNotepad UI" ON)

function(configure_lightnote_target target_name)
    target_compile_features(${target_name} PRIVATE cxx_std_17)

    if(MSVC)
        target_compile_options(${target_name} PRIVATE /W4 /permissive- /utf-8)
        target_compile_definitions(${target_name} PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
        set_property(TARGET ${target_name} PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
endfunction()

add_library(lightnote_core STATIC
    client/config.cpp
    client/discovery.cpp
    client/document_model.cpp
    client/editor_diff.cpp
    client/logger.cpp
    client/paths.cpp
    client/protocol.cpp
    client/storage.cpp
    client/sync_client.cpp
    client/sync_server.cpp
    client/text_encoding.cpp
    client/winsock_runtime.cpp
)

target_include_directories(lightnote_core PUBLIC client)
configure_lightnote_target(lightnote_core)

add_executable(ShareNotepad WIN32
    client/main.cpp
    client/app.cpp
    client/self_install.cpp
    client/resources.rc
)

if(SHARENOTEPAD_USE_WEBVIEW2_UI)
    set(WEBVIEW2_PACKAGE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/Microsoft.Web.WebView2" CACHE PATH "Microsoft.Web.WebView2 package directory")
    set(WEBVIEW2_ARCH "x64")
    if(CMAKE_VS_PLATFORM_NAME STREQUAL "Win32")
        set(WEBVIEW2_ARCH "x86")
    elseif(CMAKE_VS_PLATFORM_NAME STREQUAL "ARM64")
        set(WEBVIEW2_ARCH "arm64")
    endif()

    set(WEBVIEW2_INCLUDE_DIR "${WEBVIEW2_PACKAGE_DIR}/build/native/include")
    set(WEBVIEW2_LIB "${WEBVIEW2_PACKAGE_DIR}/build/native/${WEBVIEW2_ARCH}/WebView2Loader.dll.lib")
    set(WEBVIEW2_DLL "${WEBVIEW2_PACKAGE_DIR}/build/native/${WEBVIEW2_ARCH}/WebView2Loader.dll")

    if(NOT EXISTS "${WEBVIEW2_INCLUDE_DIR}/WebView2.h")
        message(FATAL_ERROR "WebView2 SDK header not found. Download Microsoft.Web.WebView2 into ${WEBVIEW2_PACKAGE_DIR} or configure with -DSHARENOTEPAD_USE_WEBVIEW2_UI=OFF.")
    endif()

    target_sources(ShareNotepad PRIVATE
        client/webview_window.cpp
    )
    target_compile_definitions(ShareNotepad PRIVATE SHARENOTEPAD_USE_WEBVIEW2_UI)
    target_include_directories(ShareNotepad PRIVATE "${WEBVIEW2_INCLUDE_DIR}")
    target_link_libraries(ShareNotepad PRIVATE "${WEBVIEW2_LIB}")
    add_custom_command(TARGET ShareNotepad POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${WEBVIEW2_DLL}"
            "$<TARGET_FILE_DIR:ShareNotepad>/WebView2Loader.dll")
    add_custom_command(TARGET ShareNotepad POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/ui"
            "$<TARGET_FILE_DIR:ShareNotepad>/ui")
else()
    target_sources(ShareNotepad PRIVATE
        client/window.cpp
    )
endif()

configure_lightnote_target(ShareNotepad)
target_link_libraries(ShareNotepad PRIVATE lightnote_core user32 gdi32 ole32 shell32 ws2_32 dwmapi uxtheme)

add_executable(lightnote_storage_smoke
    tests/storage_smoke.cpp
)

configure_lightnote_target(lightnote_storage_smoke)
target_link_libraries(lightnote_storage_smoke PRIVATE lightnote_core)

add_executable(lightnote_config_logger_smoke
    tests/config_logger_smoke.cpp
)

configure_lightnote_target(lightnote_config_logger_smoke)
target_link_libraries(lightnote_config_logger_smoke PRIVATE lightnote_core ole32)

add_executable(lightnote_protocol_smoke
    tests/protocol_smoke.cpp
)

configure_lightnote_target(lightnote_protocol_smoke)
target_link_libraries(lightnote_protocol_smoke PRIVATE lightnote_core)

add_executable(lightnote_sync_smoke
    tests/sync_smoke.cpp
)

configure_lightnote_target(lightnote_sync_smoke)
target_link_libraries(lightnote_sync_smoke PRIVATE lightnote_core ole32 ws2_32)

add_executable(lightnote_sync_large_smoke
    tests/sync_large_smoke.cpp
)

configure_lightnote_target(lightnote_sync_large_smoke)
target_link_libraries(lightnote_sync_large_smoke PRIVATE lightnote_core ole32 ws2_32)

add_executable(lightnote_sync_security_smoke
    tests/sync_security_smoke.cpp
)

configure_lightnote_target(lightnote_sync_security_smoke)
target_link_libraries(lightnote_sync_security_smoke PRIVATE lightnote_core ole32 ws2_32)

add_executable(lightnote_discovery_smoke
    tests/discovery_smoke.cpp
)

configure_lightnote_target(lightnote_discovery_smoke)
target_link_libraries(lightnote_discovery_smoke PRIVATE lightnote_core ole32 ws2_32)

add_executable(lightnote_editor_diff_smoke
    tests/editor_diff_smoke.cpp
)

configure_lightnote_target(lightnote_editor_diff_smoke)
target_link_libraries(lightnote_editor_diff_smoke PRIVATE lightnote_core)

add_executable(lightnote_document_model_smoke
    tests/document_model_smoke.cpp
)

configure_lightnote_target(lightnote_document_model_smoke)
target_link_libraries(lightnote_document_model_smoke PRIVATE lightnote_core)
