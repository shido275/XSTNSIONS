cmake_minimum_required(VERSION 3.22)
project(XSTNSIONS VERSION 0.1.0 LANGUAGES C CXX)

include(CTest)
option(XSTNSIONS_BUILD_PLUGINS "Build JUCE standalone and DAW plugin targets" ON)

add_library(xstnsions_engine INTERFACE)
target_include_directories(xstnsions_engine INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/Source)
target_compile_features(xstnsions_engine INTERFACE cxx_std_17)

if(BUILD_TESTING)
    add_executable(xstnsions_engine_tests Tests/AudioEngineTests.cpp)
    target_link_libraries(xstnsions_engine_tests PRIVATE xstnsions_engine)
    target_compile_definitions(xstnsions_engine_tests PRIVATE
        XSTNSIONS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME audio_engine COMMAND xstnsions_engine_tests)
endif()

if(XSTNSIONS_BUILD_PLUGINS)
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 9.0.4
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(JUCE)

    juce_add_binary_data(XSTNSIONS_BundledExtensions SOURCES
        extensions/aurora-keys/extension.json
        extensions/velvet-pulse/extension.json)
    target_compile_definitions(XSTNSIONS_BundledExtensions INTERFACE
        JUCE_XSTNSIONS_EXTENSION_PREFIX="${CMAKE_CURRENT_SOURCE_DIR}/extensions/")
    target_compile_definitions(xstnsions_engine INTERFACE
        JUCE_XSTNSIONS_EXTENSION_PREFIX="${CMAKE_CURRENT_SOURCE_DIR}/extensions/")

    function(add_xstnsions_plugin target product code is_synth)
        juce_add_plugin(${target}
            COMPANY_NAME "XSTNSIONS Audio"
            BUNDLE_ID "audio.xstnsions.${target}"
            PLUGIN_MANUFACTURER_CODE Xtns
            PLUGIN_CODE ${code}
            PRODUCT_NAME "${product}"
            DESCRIPTION "XSTNSIONS modular instrument and effect"
            FORMATS Standalone VST3 AU
            IS_SYNTH ${is_synth}
            NEEDS_MIDI_INPUT ${is_synth}
            NEEDS_MIDI_OUTPUT FALSE
            NEEDS_WEB_BROWSER FALSE
            NEEDS_CURL FALSE
            COPY_PLUGIN_AFTER_BUILD FALSE
        )
        juce_generate_juce_header(${target})
        target_sources(${target} PRIVATE Source/PluginProcessor.cpp Source/PluginEditor.cpp)
        target_include_directories(${target} PRIVATE Source)
        target_compile_definitions(${target} PRIVATE
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            JUCE_VST3_CAN_REPLACE_VST2=0
            XSTNSIONS_INSTRUMENT=$<BOOL:${is_synth}>
            JUCE_XSTNSIONS_EXTENSION_PREFIX="${CMAKE_CURRENT_SOURCE_DIR}/extensions/"
        )
        target_link_libraries(${target} PRIVATE
            xstnsions_engine
            XSTNSIONS_BundledExtensions
            juce::juce_audio_utils
            juce::juce_dsp
            PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags
        )
    endfunction()

    add_xstnsions_plugin(XSTNSIONS_Instrument "XSTNSIONS Instrument" Xtsi TRUE)
    add_xstnsions_plugin(XSTNSIONS_Effect "XSTNSIONS Effect" Xtef FALSE)

    include(GNUInstallDirs)
    include(CPackComponent)
    set(CPACK_PACKAGE_NAME "XSTNSIONS")
    set(CPACK_PACKAGE_VENDOR "XSTNSIONS Audio")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "A modular instrument and effect audio plug-in")
    set(CPACK_PACKAGE_CONTACT "XSTNSIONS Audio")
    set(CPACK_PACKAGE_INSTALL_DIRECTORY "XSTNSIONS")
    set(XSTNSIONS_PACKAGE_VERSION "${PROJECT_VERSION}" CACHE STRING "Version used in installer package names")
    set(CPACK_PACKAGE_VERSION "${XSTNSIONS_PACKAGE_VERSION}")
    set(CPACK_INSTALL_CMAKE_PROJECTS "${CMAKE_BINARY_DIR};${PROJECT_NAME};ALL;/")
    set(CPACK_PACKAGE_FILE_NAME "XSTNSIONS-${XSTNSIONS_PACKAGE_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")

    if(WIN32)
        # CPack's component install path directs VST3 bundles into the standard 64-bit location.
        set(XSTNSIONS_VST3_INSTALL_DIR ".")
        set(CPACK_GENERATOR "NSIS")
        set(CPACK_NSIS_COMPONENT_INSTALL ON)
        set(CPACK_NSIS_PLUGINS_INSTALL_DIRECTORY "$COMMONFILES64\\VST3")
        set(CPACK_NSIS_DISPLAY_NAME "XSTNSIONS Audio Plugins")
        set(CPACK_NSIS_PACKAGE_NAME "XSTNSIONS VST3 Plugins")
    elseif(APPLE)
        set(XSTNSIONS_VST3_INSTALL_DIR "Library/Audio/Plug-Ins/VST3")
        set(CPACK_GENERATOR "DragNDrop")
        set(CPACK_PACKAGING_INSTALL_PREFIX "/")
    else()
        set(XSTNSIONS_VST3_INSTALL_DIR "lib/vst3")
        set(CPACK_GENERATOR "DEB")
        set(CPACK_DEBIAN_PACKAGE_MAINTAINER "XSTNSIONS Audio")
        set(CPACK_DEBIAN_PACKAGE_SECTION "sound")
        set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
        set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
        set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
        set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
    endif()

    install(DIRECTORY
        "$<TARGET_PROPERTY:XSTNSIONS_Instrument_VST3,JUCE_PLUGIN_ARTEFACT_FILE>"
        DESTINATION "${XSTNSIONS_VST3_INSTALL_DIR}"
        COMPONENT Plugins)
    install(DIRECTORY
        "$<TARGET_PROPERTY:XSTNSIONS_Effect_VST3,JUCE_PLUGIN_ARTEFACT_FILE>"
        DESTINATION "${XSTNSIONS_VST3_INSTALL_DIR}"
        COMPONENT Plugins)
    if(APPLE)
        install(DIRECTORY
            "$<TARGET_PROPERTY:XSTNSIONS_Instrument_AU,JUCE_PLUGIN_ARTEFACT_FILE>"
            DESTINATION "Library/Audio/Plug-Ins/Components"
            COMPONENT Plugins)
        install(DIRECTORY
            "$<TARGET_PROPERTY:XSTNSIONS_Effect_AU,JUCE_PLUGIN_ARTEFACT_FILE>"
            DESTINATION "Library/Audio/Plug-Ins/Components"
            COMPONENT Plugins)
    else()
        cpack_add_component(Plugins DISPLAY_NAME "VST3 plug-ins" REQUIRED)
    endif()

    include(CPack)
endif()
