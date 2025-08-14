cmake_minimum_required(VERSION 3.15.2)

set(MODULE_NAME "rs_gstreamer")

# Find required packages
find_package(PkgConfig REQUIRED)
pkg_check_modules(GSTREAMER REQUIRED gstreamer-1.0)
pkg_check_modules(GSTREAMER_VIDEO REQUIRED gstreamer-video-1.0)
pkg_check_modules(GSTREAMER_AUDIO REQUIRED gstreamer-audio-1.0)
find_package(realsense2 REQUIRED)

# DeepStream paths (adjust if needed)
set(DEEPSTREAM_PATH "/opt/nvidia/deepstream/deepstream-7.1")

# Source files
set(SOURCES
    gstrealsenseplugin.cpp
    gstrealsensesrc.cpp
)

# Header files (for IDEs)
set(HEADERS
    gstrealsensesrc.h
)

add_library(gstrealsensesrc SHARED ${SOURCES} ${HEADERS})

# Link libraries
 target_link_libraries(gstrealsensesrc
    ${GSTREAMER_LIBRARIES}
    ${GSTREAMER_VIDEO_LIBRARIES}
    ${GSTREAMER_AUDIO_LIBRARIES}
    realsense2::realsense2
)

# Include directories
 target_include_directories(gstrealsensesrc PRIVATE
    ${GSTREAMER_INCLUDE_DIRS}
    ${GSTREAMER_VIDEO_INCLUDE_DIRS}
    ${GSTREAMER_AUDIO_INCLUDE_DIRS}
    ${realsense2_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Compiler flags
 target_compile_options(gstrealsensesrc PRIVATE
    -Wall
    -Wextra
    -Wno-error=unused-parameter
    -fPIC
)

# Set output name and install rules
set_target_properties(gstrealsensesrc PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${DEEPSTREAM_PATH}/lib/gst-plugins"
    PREFIX "lib"
)

install(TARGETS gstrealsensesrc
    LIBRARY DESTINATION "${DEEPSTREAM_PATH}/lib/gst-plugins"
)

# Install headers (optional, for development)
install(FILES ${HEADERS} DESTINATION include/${MODULE_NAME})