if (NOT DEFINED ASSET_SOURCE_DIR OR ASSET_SOURCE_DIR STREQUAL "")
    message(FATAL_ERROR "ASSET_SOURCE_DIR is required")
endif ()

if (NOT DEFINED DEPLOY_DIR OR DEPLOY_DIR STREQUAL "")
    message(FATAL_ERROR "DEPLOY_DIR is required")
endif ()

if (NOT EXISTS "${ASSET_SOURCE_DIR}")
    message(STATUS "Skipping asset deploy because ${ASSET_SOURCE_DIR} does not exist")
    return()
endif ()

include("${CMAKE_CURRENT_LIST_DIR}/copy_skyrim_assets.cmake")

copy_base_skyrim_assets("${ASSET_SOURCE_DIR}" "${DEPLOY_DIR}")

set(optional_asset_dir "${ASSET_SOURCE_DIR}/optional")
if (EXISTS "${optional_asset_dir}")
    if (DEFINED DEPLOY_DIR_OPTIONAL AND NOT DEPLOY_DIR_OPTIONAL STREQUAL "")
        message(STATUS "Deploying optional assets to: ${DEPLOY_DIR_OPTIONAL}")
        copy_optional_skyrim_assets("${ASSET_SOURCE_DIR}" "${DEPLOY_DIR_OPTIONAL}" optional_assets_copied)
    else ()
        message(STATUS "Skipping optional asset deploy because DEPLOY_DIR_OPTIONAL is unset")
    endif ()
endif ()
