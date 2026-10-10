# VR/OpenXR feature configuration for OpenJKDF2
# Added: VR support build configuration

if(TARGET_ANDROID)
    # Android/Quest VR: Build OpenXR loader for Android
    message(STATUS "Configuring VR for Android/Quest...")

    # Build the OpenXR loader for Android
    include(build_openxr)
else()
    # Desktop VR: Build or find OpenXR loader
    find_package(OpenXR 1.0 QUIET)

    if(NOT OpenXR_FOUND)
        message(STATUS "OpenXR not found on system, fetching from source...")
        include(build_openxr)
    else()
        message(STATUS "Using system OpenXR: ${OpenXR_VERSION}")
        set(OPENXR_INCLUDE_DIR ${OpenXR_INCLUDE_DIRS})
        set(OPENXR_LIBRARY ${OpenXR_LIBRARIES})
    endif()
endif()

# Add VR source files. CONFIGURE_DEPENDS re-globs on every build: without it a new file is
# silently left out (and fails to link) until CMake happens to be re-run, which the gradle
# build's cached .cxx tree never does on its own.
file(GLOB VR_SRCS CONFIGURE_DEPENDS
    ${PROJECT_SOURCE_DIR}/src/Platform/VR/*.c
    ${PROJECT_SOURCE_DIR}/src/Platform/VR/*.cpp
)
list(APPEND ENGINE_SOURCE_FILES ${VR_SRCS})

# Add VR include directories
include_directories(${OPENXR_INCLUDE_DIR})

# Add compile definitions for VR support
add_compile_definitions(PLATFORM_VR)
add_compile_definitions(TARGET_USE_OPENXR)
# Single-pass stereo via OpenGL MultiView (GL_OVR_multiview2) is now the unified VR
# rendering path on both Android/Quest and desktop PCVR. This global definition lets all
# translation units (std3D.c, sithCamera.c, jkGame.c, stdVR*.c) gate the shared multiview
# code on MULTIVIEW_ENABLED instead of the platform-specific TARGET_ANDROID_NATIVE_GLES.
add_compile_definitions(MULTIVIEW_ENABLED)

# Link OpenXR library
macro(plat_vr_link_deps)
    if(TARGET_ANDROID)
        # On Android/Quest, link against the built OpenXR loader
        target_link_libraries(${BIN_NAME} PRIVATE ${OPENXR_LIBRARY})
        target_link_libraries(${BIN_NAME} PRIVATE EGL GLESv3)
    else()
        target_link_libraries(${BIN_NAME} PRIVATE ${OPENXR_LIBRARY})
    endif()
endmacro()

message(STATUS "VR support enabled with OpenXR")
