cmake_minimum_required(VERSION 3.16)
project(XRPerfOverlay LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Runtime C++ lié statiquement : la DLL ne dépend d'aucun redistribuable et ne partage
# rien avec le runtime C++ du jeu.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Numéro de version : une seule source, le fichier VERSION à la racine du projet (forme X.Y.Z).
set(XRPO_VERSION "0.0.0")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../VERSION")
    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/../VERSION" XRPO_VERSION LIMIT_COUNT 1)
    string(STRIP "${XRPO_VERSION}" XRPO_VERSION)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/../VERSION")
endif()
if(NOT XRPO_VERSION MATCHES "^([0-9]+)[.]([0-9]+)[.]([0-9]+)$")
    message(FATAL_ERROR "Le fichier VERSION doit contenir un numéro de la forme 1.2.3 (lu : '${XRPO_VERSION}').")
endif()
set(XRPO_VERSION_MAJOR ${CMAKE_MATCH_1})
set(XRPO_VERSION_MINOR ${CMAKE_MATCH_2})
set(XRPO_VERSION_PATCH ${CMAKE_MATCH_3})
message(STATUS "XR Perf Overlay ${XRPO_VERSION}")
configure_file(src/version_info.h.in "${CMAKE_CURRENT_BINARY_DIR}/generated/version_info.h" @ONLY)

option(XRPO_BUILD_TESTS "Compiler l'outil de test et d'aperçu du panneau (tests/preview.cpp)" OFF)

add_library(XR_APILAYER_NOVENDOR_perf_overlay SHARED
    src/layer.cpp
    src/overlay.cpp
    src/gfx_d3d11.cpp
    src/gfx_d3d12.cpp
    src/panel.cpp
    src/config.cpp
    src/log.cpp
    src/sensors_reader.cpp
    src/summary_file.cpp
    src/panel_share.cpp
    src/settings_reader.cpp
    src/preview_api.cpp
    src/hardware_info.cpp
    src/hand_input.cpp
    src/layer_presence.cpp
    src/window_share.cpp
    src/version.rc
    exports.def
)
target_include_directories(XR_APILAYER_NOVENDOR_perf_overlay PRIVATE external "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_compile_definitions(XR_APILAYER_NOVENDOR_perf_overlay PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE XRPO_VERSION="${XRPO_VERSION}")
set_target_properties(XR_APILAYER_NOVENDOR_perf_overlay PROPERTIES PREFIX "")

if(MSVC)
    target_compile_options(XR_APILAYER_NOVENDOR_perf_overlay PRIVATE /W4 /utf-8 /EHsc)
else()
    # MinGW / clang : tout lier statiquement pour n'avoir aucune DLL annexe.
    target_compile_options(XR_APILAYER_NOVENDOR_perf_overlay PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
    target_link_options(XR_APILAYER_NOVENDOR_perf_overlay PRIVATE -static)
endif()

# XRPerfVR : panneau, montre et rapports dans les jeux SteamVR (OpenVR). Programme sans fenêtre,
# lancé par XRPerfSensors ; openvr_api.dll (Valve, licence BSD) est copiée à côté.
add_executable(XRPerfVR WIN32
    src/vr_main.cpp
    src/vr_host.cpp
    src/openvr_backend.cpp
    src/layer_presence.cpp
    src/window_share.cpp
    src/panel.cpp
    src/config.cpp
    src/log.cpp
    src/sensors_reader.cpp
    src/settings_reader.cpp
    src/summary_file.cpp
    src/panel_share.cpp
    src/hardware_info.cpp
)
target_include_directories(XRPerfVR PRIVATE external "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_compile_definitions(XRPerfVR PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE _WIN32_WINNT=0x0A00 XRPO_VERSION="${XRPO_VERSION}")
target_link_libraries(XRPerfVR PRIVATE shell32 d3d11 dxgi)
if(MSVC)
    target_compile_options(XRPerfVR PRIVATE /W4 /utf-8 /EHsc)
else()
    target_compile_options(XRPerfVR PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
    target_link_options(XRPerfVR PRIVATE -static -municode)
endif()
add_custom_command(TARGET XRPerfVR POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/external/openvr/bin/win64/openvr_api.dll" "$<TARGET_FILE_DIR:XRPerfVR>")

# XRPerfCapture.dll : capture d'une fenêtre du bureau (un chat...) pour l'afficher dans le casque.
# Chargée par XRPerfSensors (rangée à côté de lui) ; l'image passe par une mémoire partagée.
add_library(XRPerfCapture SHARED src/window_capture.cpp)
target_include_directories(XRPerfCapture PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_compile_definitions(XRPerfCapture PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE _WIN32_WINNT=0x0A00)
target_link_libraries(XRPerfCapture PRIVATE d3d11 dxgi runtimeobject)
set_target_properties(XRPerfCapture PROPERTIES PREFIX "")
if(MSVC)
    target_compile_options(XRPerfCapture PRIVATE /W4 /utf-8 /EHsc)
else()
    target_compile_options(XRPerfCapture PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
    target_link_options(XRPerfCapture PRIVATE -static)
endif()

if(XRPO_BUILD_TESTS)
    add_executable(xrpo_preview ../tests/preview.cpp src/panel.cpp src/preview_api.cpp)
    if(MSVC)
        target_compile_options(xrpo_preview PRIVATE /utf-8 /EHsc)
    endif()
endif()
