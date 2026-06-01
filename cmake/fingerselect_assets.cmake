function(lhrs_add_fingerselect_swf target_name output_name xml_name use_skyui_button_art)
    set(fingerselect_source_dir "${CMAKE_CURRENT_SOURCE_DIR}/interface/fingerselect")
    set(fingerselect_build_dir "${CMAKE_CURRENT_BINARY_DIR}/interface/fingerselect")
    set(fingerselect_as_template "${fingerselect_source_dir}/actionscript/FingerSelectMenu.as.in")
    set(fingerselect_output_dir "${GENERATED_ASSET_SOURCE_DIR}/Interface")

    set(script_root "${fingerselect_build_dir}/${target_name}/scripts")
    set(script_file "${script_root}/frame_1/DoAction.as")
    set(base_swf "${fingerselect_build_dir}/${target_name}/base/${output_name}")
    set(output_swf "${fingerselect_output_dir}/${output_name}")
    set(source_xml "${fingerselect_source_dir}/swf/${xml_name}")

    set(LHRS_USE_SKYUI_BUTTON_ART "${use_skyui_button_art}")
    file(MAKE_DIRECTORY "${script_root}/frame_1")
    configure_file("${fingerselect_as_template}" "${script_file}" @ONLY)

    add_custom_command(
            OUTPUT "${output_swf}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory
                    "${fingerselect_build_dir}/${target_name}/base"
                    "${fingerselect_output_dir}"
            COMMAND "${CMAKE_COMMAND}" -E echo "Building ${output_name}"
            COMMAND "${FFDEC_CLI}" -xml2swf "${source_xml}" "${base_swf}"
            COMMAND "${FFDEC_CLI}"
                    -config autoDeobfuscate=false,decompile=false
                    -onerror abort
                    -importScript "${base_swf}" "${output_swf}" "${script_root}"
            DEPENDS
                    "${source_xml}"
                    "${script_file}"
                    "${fingerselect_as_template}"
            VERBATIM
    )

    add_custom_target("${target_name}" DEPENDS "${output_swf}")
endfunction()

function(lhrs_add_fingerselect_assets plugin_target)
    if (NOT FFDEC_CLI)
        message(FATAL_ERROR "lhrs_add_fingerselect_assets: FFDEC_CLI is not set")
    endif ()

    lhrs_add_fingerselect_swf("${plugin_target}_fingerselect_vanilla" lhrs_fingerselect.swf fingerselect.xml false)
    lhrs_add_fingerselect_swf("${plugin_target}_fingerselect_skyui" lhrs_fingerselect_skyui.swf fingerselect_skyui.xml true)

    add_custom_target(
            "${plugin_target}_interface"
            DEPENDS
            "${plugin_target}_fingerselect_vanilla"
            "${plugin_target}_fingerselect_skyui"
    )

    add_dependencies("${plugin_target}" "${plugin_target}_interface")
endfunction()
