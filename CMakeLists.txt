cmake_minimum_required(VERSION 3.24...4.2)

set(PLUGIN_AUTHOR "GabonZ" CACHE STRING "Plugin author name")
set(PLUGIN_LICENSE "GPL-3.0-only" CACHE STRING "Plugin license")

set(VCPKG_DISABLE_METRICS ON)
set(VCPKG_INSTALL_OPTIONS "--no-print-usage")

project(
        LeftHandRingsSKSE
        VERSION 0.4.3
        DESCRIPTION "Native multi-ring support"
        LANGUAGES CXX
)

set(OPTIONAL_ASSETS_VERSION 0.3.0)

find_program(CLANG_TIDY_EXECUTABLE NAMES clang-tidy)
find_program(RUN_CLANG_TIDY_EXECUTABLE NAMES run-clang-tidy run-clang-tidy.py)
find_package(Python3 COMPONENTS Interpreter QUIET)

if (PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
    message(FATAL_ERROR "In-source builds not allowed. Please make a build directory and run CMake from there.")
endif ()

if (CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    set(CMAKE_CXX_EXTENSIONS OFF)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif ()

include(CheckIPOSupported)
check_ipo_supported(RESULT SUPPORTS_IPO OUTPUT IPO_OUTPUT)
if (SUPPORTS_IPO)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO ON)
endif ()

set(DEPLOY_DIR "" CACHE PATH "Deployment directory for base mod assets")
set(DEPLOY_DIR_OPTIONAL "" CACHE PATH "Deployment directory for optional mod assets")
set(ASSET_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Skyrim/Data")
set(GENERATED_ASSET_SOURCE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated-assets")

find_program(FFDEC_CLI
        NAMES ffdec-cli ffdec-cli.exe
        PATHS "${CMAKE_CURRENT_SOURCE_DIR}/tools/FFDec"
        NO_DEFAULT_PATH
)

if (NOT FFDEC_CLI)
    find_program(FFDEC_CLI NAMES ffdec-cli ffdec-cli.exe)
endif ()

if (FFDEC_CLI)
    message(STATUS "FFDec CLI: ${FFDEC_CLI}")
else ()
    message(FATAL_ERROR "FFDec CLI not found")
endif ()

add_subdirectory(extern/CLibUtil)
add_subdirectory(extern/CommonLibSSE-NG)

include(extern/CommonLibSSE-NG/cmake/CommonLibSSE.cmake)

configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.rc.in"
        "${CMAKE_CURRENT_BINARY_DIR}/version.rc"
        @ONLY
)

set(SOURCES
        src/Audio/EquipSounds.h
        src/Compatibility/Vanilla.cpp
        src/Compatibility/Vanilla.h
        src/Core/ActorKey.h
        src/Core/Assignment.h
        src/Core/TargetMask.h
        src/Core/TargetProjection.h
        src/Core/ItemSource.h
        src/Core/Target.h
        src/Equipment/AssignmentStore.cpp
        src/Equipment/AssignmentStore.h
        src/Equipment/AutoEquip.cpp
        src/Equipment/AutoEquip.h
        src/Equipment/AssignmentActions.cpp
        src/Equipment/AssignmentActions.h
        src/Equipment/RaceSwitchRestore.cpp
        src/Equipment/RaceSwitchRestore.h
        src/Papyrus/ScriptEventMirror.cpp
        src/Papyrus/ScriptEventMirror.h
        src/EventListener.cpp
        src/EventListener.h
        src/HookUtil.h
        src/Hooks.cpp
        src/Hooks.h
        src/Inventory.cpp
        src/Inventory.h
        src/Localization.cpp
        src/Localization.h
        src/main.cpp
        src/Papyrus/FormScriptNames.cpp
        src/Papyrus/FormScriptNames.h
        src/VirtualSlots/EnchantmentEffects.cpp
        src/VirtualSlots/EnchantmentEffects.h
        src/SourceModelFootprints.cpp
        src/SourceModelFootprints.h
        src/Visuals/Attachments.cpp
        src/Visuals/Attachments.h
        src/Papyrus.cpp
        src/Papyrus.h
        src/PCH.h
        src/Serialization.cpp
        src/Serialization.h
        src/Settings.cpp
        src/Settings.h
        src/UI.cpp
        src/UI.h
        src/UI/ContainerMenu.cpp
        src/UI/ContainerMenu.h
        src/UI/FavoritesMenu.cpp
        src/UI/FavoritesMenu.h
        src/UI/FingerSelectMenu.cpp
        src/UI/FingerSelectMenu.h
        src/UI/InventoryMenu.cpp
        src/UI/InventoryMenu.h
        src/UI/ItemMenuActions.cpp
        src/UI/ItemMenuActions.h
        src/UI/RingItemRows.cpp
        src/UI/RingItemRows.h
        src/UI/Scaleform.h
        src/UI/VanillaItemMenuControls.cpp
        src/UI/VanillaItemMenuControls.h
        src/VirtualSlots.cpp
        src/VirtualSlots.h
        src/ModelPaths.h
        "${CMAKE_CURRENT_BINARY_DIR}/version.rc"
)

add_commonlibsse_plugin(
        ${PROJECT_NAME}
        SOURCES ${SOURCES}
        AUTHOR "${PLUGIN_AUTHOR}"
)

include(cmake/fingerselect_assets.cmake)
lhrs_add_fingerselect_assets(${PROJECT_NAME})

target_compile_features(
        ${PROJECT_NAME}
        PUBLIC
        cxx_std_23
)

target_include_directories(
        ${PROJECT_NAME}
        PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

set_target_properties(CLibUtil PROPERTIES
        INTERFACE_SYSTEM_INCLUDE_DIRECTORIES $<TARGET_PROPERTY:CLibUtil,INTERFACE_INCLUDE_DIRECTORIES>
)

target_link_libraries(
        ${PROJECT_NAME}
        PUBLIC
        CommonLibSSE::CommonLibSSE
        CLibUtil::CLibUtil
)

target_precompile_headers(
        ${PROJECT_NAME}
        PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src/PCH.h"
)

if (MSVC)
    target_compile_options(
            ${PROJECT_NAME}
            PRIVATE
            /EHsc
            /W4
            $<$<CXX_COMPILER_ID:MSVC>:/WX>
            $<$<COMPILE_LANG_AND_ID:CXX,Clang>:-Wno-unused-command-line-argument>
            /utf-8
            /permissive-
            $<$<CONFIG:RelWithDebInfo>:/Ox>
    )

    target_link_options(
            ${PROJECT_NAME}
            PRIVATE
            $<$<CONFIG:Debug>:/DEBUG>
            $<$<CONFIG:Release>:/DEBUG>
            $<$<CONFIG:RelWithDebInfo>:/DEBUG>
            $<$<CONFIG:RelWithDebInfo>:/OPT:REF>
            $<$<CONFIG:RelWithDebInfo>:/OPT:ICF>
            $<$<CONFIG:RelWithDebInfo>:/INCREMENTAL:NO>
    )
endif ()

add_custom_command(
        TARGET ${PROJECT_NAME}
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${CMAKE_CURRENT_BINARY_DIR}/.cache"
        VERBATIM
)

if (DEPLOY_DIR)
    foreach (deploy_dir IN LISTS DEPLOY_DIR)
        set(PLUGIN_DIR "${deploy_dir}/SKSE/Plugins")

        add_custom_command(
                TARGET ${PROJECT_NAME}
                POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E echo "Deploying to: ${PLUGIN_DIR}"
                COMMAND ${CMAKE_COMMAND} -E make_directory "${PLUGIN_DIR}"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${PROJECT_NAME}>" "${PLUGIN_DIR}"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_PDB_FILE:${PROJECT_NAME}>" "${PLUGIN_DIR}"
                VERBATIM
        )
    endforeach ()

    add_custom_command(
            TARGET ${PROJECT_NAME}
            POST_BUILD
            COMMAND "${CMAKE_COMMAND}"
            "-DASSET_SOURCE_DIR=${ASSET_SOURCE_DIR}"
            "-DGENERATED_ASSET_SOURCE_DIR=${GENERATED_ASSET_SOURCE_DIR}"
            "-DDEPLOY_DIR=${DEPLOY_DIR}"
            "-DDEPLOY_DIR_OPTIONAL=${DEPLOY_DIR_OPTIONAL}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/deploy_assets.cmake"
            VERBATIM
    )
endif ()

set(DIST_OUTPUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/dist" CACHE PATH "Directory where release ZIP files are written")

add_custom_target(
        ${PROJECT_NAME}_dist
        COMMAND "${CMAKE_COMMAND}"
        "-DMOD_NAME=${PROJECT_NAME}"
        "-DMOD_VERSION=${PROJECT_VERSION}"
        "-DOPTIONAL_ASSETS_VERSION=${OPTIONAL_ASSETS_VERSION}"
        "-DPLUGIN_DLL=$<TARGET_FILE:${PROJECT_NAME}>"
        "-DPLUGIN_PDB=$<TARGET_PDB_FILE:${PROJECT_NAME}>"
        "-DASSET_SOURCE_DIR=${ASSET_SOURCE_DIR}"
        "-DGENERATED_ASSET_SOURCE_DIR=${GENERATED_ASSET_SOURCE_DIR}"
        "-DOUTPUT_DIR=${DIST_OUTPUT_DIR}"
        "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/dist/$<CONFIG>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/package_mod.cmake"
        DEPENDS ${PROJECT_NAME}
        COMMENT "Creating ${PROJECT_NAME} distribution archives"
        VERBATIM
)

if (CLANG_TIDY_EXECUTABLE AND
        CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND
        CMAKE_GENERATOR STREQUAL "Ninja" AND
        NOT CMAKE_CONFIGURATION_TYPES)
    set(PROJECT_PCH_FILE "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/${PROJECT_NAME}.dir/cmake_pch.cxx.pch")

    add_custom_target(
            ${PROJECT_NAME}_clang_tidy
            COMMAND "${CMAKE_COMMAND}"
            "-DCLANG_TIDY_EXECUTABLE=${CLANG_TIDY_EXECUTABLE}"
            "-DRUN_CLANG_TIDY_EXECUTABLE=${RUN_CLANG_TIDY_EXECUTABLE}"
            "-DPython3_EXECUTABLE=${Python3_EXECUTABLE}"
            "-DCLANG_TIDY_JOBS=8"
            "-DCOMPILE_COMMANDS_FILE=${CMAKE_BINARY_DIR}/compile_commands.json"
            "-DBUILD_DIR=${CMAKE_BINARY_DIR}"
            "-DSOURCE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/src"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_clang_tidy.cmake"
            DEPENDS "${PROJECT_PCH_FILE}"
            COMMENT "Running clang-tidy on ${PROJECT_NAME} source files"
            VERBATIM
    )
elseif (CLANG_TIDY_EXECUTABLE)
    add_custom_target(
            ${PROJECT_NAME}_clang_tidy
            COMMAND "${CMAKE_COMMAND}" -E echo "clang-tidy requires the ninja-lint preset because it uses clang-cl-compatible precompiled headers."
            COMMAND "${CMAKE_COMMAND}" -E false
            COMMENT "clang-tidy requires the ninja-lint preset"
            VERBATIM
    )
else ()
    add_custom_target(
            ${PROJECT_NAME}_clang_tidy
            COMMAND "${CMAKE_COMMAND}" -E false
            COMMENT "clang-tidy was not found"
            VERBATIM
    )
endif ()
