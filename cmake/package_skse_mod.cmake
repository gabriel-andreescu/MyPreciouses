if (NOT DEFINED MOD_NAME OR MOD_NAME STREQUAL "")
    message(FATAL_ERROR "MOD_NAME is required")
endif ()

if (NOT DEFINED MOD_VERSION OR MOD_VERSION STREQUAL "")
    message(FATAL_ERROR "MOD_VERSION is required")
endif ()

if (NOT DEFINED PLUGIN_DLL OR NOT EXISTS "${PLUGIN_DLL}")
    message(FATAL_ERROR "PLUGIN_DLL does not exist: ${PLUGIN_DLL}")
endif ()

if (NOT DEFINED PLUGIN_PDB OR NOT EXISTS "${PLUGIN_PDB}")
    message(FATAL_ERROR "PLUGIN_PDB does not exist: ${PLUGIN_PDB}")
endif ()

if (NOT DEFINED OUTPUT_DIR OR OUTPUT_DIR STREQUAL "")
    message(FATAL_ERROR "OUTPUT_DIR is required")
endif ()

if (NOT DEFINED WORK_DIR OR WORK_DIR STREQUAL "")
    message(FATAL_ERROR "WORK_DIR is required")
endif ()

set(main_zip "${OUTPUT_DIR}/${MOD_NAME}-${MOD_VERSION}.zip")
set(optional_zip "${OUTPUT_DIR}/${MOD_NAME}-${MOD_VERSION}-optional.zip")
set(main_stage "${WORK_DIR}/main")
set(optional_stage "${WORK_DIR}/optional")

include("${CMAKE_CURRENT_LIST_DIR}/copy_skyrim_assets.cmake")

file(MAKE_DIRECTORY "${OUTPUT_DIR}")
file(REMOVE_RECURSE "${WORK_DIR}")
file(REMOVE "${main_zip}" "${optional_zip}")

file(MAKE_DIRECTORY "${main_stage}/SKSE/Plugins")
file(COPY "${PLUGIN_DLL}" DESTINATION "${main_stage}/SKSE/Plugins")
file(COPY "${PLUGIN_PDB}" DESTINATION "${main_stage}/SKSE/Plugins")

if (DEFINED ASSET_SOURCE_DIR)
    copy_base_skyrim_assets("${ASSET_SOURCE_DIR}" "${main_stage}")
endif ()

file(GLOB main_entries RELATIVE "${main_stage}" "${main_stage}/*")
execute_process(
        COMMAND "${CMAKE_COMMAND}" -E tar cf "${main_zip}" --format=zip ${main_entries}
        WORKING_DIRECTORY "${main_stage}"
        RESULT_VARIABLE main_zip_result
)

if (NOT main_zip_result EQUAL 0)
    message(FATAL_ERROR "Failed to create ${main_zip}")
endif ()

message(STATUS "Created ${main_zip}")

if (DEFINED ASSET_SOURCE_DIR AND EXISTS "${ASSET_SOURCE_DIR}/optional")
    copy_optional_skyrim_assets("${ASSET_SOURCE_DIR}" "${optional_stage}" optional_assets_copied)

    if (optional_assets_copied)
        file(GLOB optional_entries RELATIVE "${optional_stage}" "${optional_stage}/*")
        execute_process(
                COMMAND "${CMAKE_COMMAND}" -E tar cf "${optional_zip}" --format=zip ${optional_entries}
                WORKING_DIRECTORY "${optional_stage}"
                RESULT_VARIABLE optional_zip_result
        )

        if (NOT optional_zip_result EQUAL 0)
            message(FATAL_ERROR "Failed to create ${optional_zip}")
        endif ()

        message(STATUS "Created ${optional_zip}")
    else ()
        message(STATUS "Skipping optional archive because ${ASSET_SOURCE_DIR}/optional is empty")
    endif ()
endif ()
