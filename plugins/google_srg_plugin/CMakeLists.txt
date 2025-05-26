cmake_minimum_required(VERSION 3.10)
project(googlesrg CXX C)

# --- UniMRCP Specifics ---
# These variables are typically set by the main UniMRCP CMakeLists.txt
# Ensure they are available or adapt as necessary.
# Example: set(UniMRCP_PLUGIN_INSTALL_DIR lib/unimrcp/plugin CACHE PATH "UniMRCP Plugin Install Directory")
if(NOT UniMRCP_PLUGIN_INSTALL_DIR)
    set(UniMRCP_PLUGIN_INSTALL_DIR lib/unimrcp/plugins) # Default if not set by parent
endif()

# --- Source Files ---
set(GOOGLE_SRG_C_SOURCES
    src/google_srg_engine.c
)
set(GOOGLE_SRG_CPP_SOURCES
    src/google_stt_streamer.cpp
)

source_group("C Sources" FILES ${GOOGLE_SRG_C_SOURCES})
source_group("C++ Sources" FILES ${GOOGLE_SRG_CPP_SOURCES})

# --- Find Dependencies ---
find_package(Protobuf REQUIRED)
find_package(gRPC CONFIG REQUIRED) # Modern gRPC usage
find_package(google_cloud_cpp_speech REQUIRED)

# --- Include Directories ---
include_directories(
    # Plugin's own include directory (if any, for headers shared between C and C++)
    # ${CMAKE_CURRENT_SOURCE_DIR}/include 
    src # Allows google_srg_engine.c to find google_stt_streamer.hpp

    # UniMRCP include directories (these should be provided by the parent UniMRCP CMake)
    ${MRCP_ENGINE_INCLUDE_DIRS}
    ${MRCP_INCLUDE_DIRS}
    ${MPF_INCLUDE_DIRS}
    ${APR_TOOLKIT_INCLUDE_DIRS}
    ${APR_INCLUDE_DIRS}
    ${APU_INCLUDE_DIRS}

    # Dependencies' include directories
    ${Protobuf_INCLUDE_DIRS}
    ${gRPC_INCLUDE_DIRS}
    ${google_cloud_cpp_speech_INCLUDE_DIRS}
)

# --- Add Plugin Library ---
add_library(${PROJECT_NAME} MODULE
    ${GOOGLE_SRG_C_SOURCES}
    ${GOOGLE_SRG_CPP_SOURCES}
)

# --- Set C++ Standard ---
# google-cloud-cpp typically requires C++17 or higher.
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 17)
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD_REQUIRED ON)
set_target_properties(${PROJECT_NAME} PROPERTIES POSITION_INDEPENDENT_CODE ON) # Good practice for shared libs

# --- Link Libraries ---
target_link_libraries(${PROJECT_NAME} PRIVATE # Use PRIVATE if these are impl details
    # Google Cloud and gRPC libraries
    google_cloud_cpp_speech::speech # Imported target from find_package
    # gRPC libraries are typically handled by linking against their main target,
    # which should bring in transitive dependencies. For gRPC found via CONFIG:
    gRPC::grpc++ # Main C++ gRPC library
    # If specific components are needed and not pulled by gRPC::grpc++, add them:
    # gRPC::grpc gRPC::gprcares (example, depends on gRPC build)
    ${Protobuf_LIBRARIES} # From find_package(Protobuf)
    
    # UniMRCP Libraries (ensure these are actual target names or library paths)
    # This is highly dependent on how UniMRCP's CMake is structured.
    # If UniMRCP uses CMake targets:
    #   unimrcp::engine unimrcp::client unimrcp::server (example names)
    # If UniMRCP provides variables like ${UNIMRCP_LIBRARIES}:
    #   ${UNIMRCP_LIBRARIES}
    # For a plugin, you typically link against components like:
    #   mrcpengine mrcp mpf aprtoolkit apr apu (if these are library names)
    # This part needs to be verified against UniMRCP's actual CMake setup.
    # Placeholder for common UniMRCP components:
    # $<TARGET_OBJECTS:mrcpengine> $<TARGET_OBJECTS:mrcp> ... (if using object libraries)

    # Standard libraries
    pthread # For std::thread
    stdc++  # For C++ standard library
)

# --- Plugin Properties ---
# UniMRCP plugins usually don't have "lib" prefix.
# The output name will be "googlesrg.so" or "googlesrg.dll".
set_target_properties(${PROJECT_NAME} PROPERTIES PREFIX "")
# Output directory for the plugin (can be overridden by UniMRCP's main CMake)
set_target_properties(${PROJECT_NAME} PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugin)
# For IDEs like Visual Studio to group plugins nicely
set_target_properties(${PROJECT_NAME} PROPERTIES FOLDER "UniMRCP/Plugins")

# --- Installation ---
install(TARGETS ${PROJECT_NAME}
    LIBRARY DESTINATION ${UniMRCP_PLUGIN_INSTALL_DIR}
    RUNTIME DESTINATION ${UniMRCP_PLUGIN_INSTALL_DIR} # For Windows DLLs
)

# PDB installation for MSVC debug symbols
if(MSVC)
    install(FILES $<TARGET_PDB_FILE:${PROJECT_NAME}>
        DESTINATION ${UniMRCP_PLUGIN_INSTALL_DIR}
        CONFIGURATIONS Debug RelWithDebInfo
        OPTIONAL
    )
endif()

# --- Compile Definitions (Optional) ---
# Example: Add a definition specific to this plugin
# target_compile_definitions(${PROJECT_NAME} PRIVATE GOOGLE_SRG_PLUGIN_BUILT)

# Add specific compile options if needed (e.g., for warnings, optimization)
# target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra)
# target_link_options(${PROJECT_NAME} PRIVATE some_linker_flag)

# --- Message ---
message(STATUS "Configuring UniMRCP Google SRG Plugin: ${PROJECT_NAME}")
message(STATUS "  > Installation directory: ${UniMRCP_PLUGIN_INSTALL_DIR}")
message(STATUS "  > Protobuf includes: ${Protobuf_INCLUDE_DIRS}")
message(STATUS "  > Protobuf libraries: ${Protobuf_LIBRARIES}")
message(STATUS "  > gRPC includes: ${gRPC_INCLUDE_DIRS}")
message(STATUS "  > gRPC libraries: ${gRPC_LIBRARIES}") # This var might not be set by gRPC CONFIG, link target is preferred
message(STATUS "  > google-cloud-cpp-speech includes: ${google_cloud_cpp_speech_INCLUDE_DIRS}")
message(STATUS "  > google-cloud-cpp-speech libraries: google_cloud_cpp_speech::speech") # Link target
# Note: The exact variable names like ${gRPC_LIBRARIES} might differ or not be set when using modern CMake imported targets.
# Linking against the imported target (e.g., gRPC::grpc++) is generally preferred and handles dependencies.
