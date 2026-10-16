cmake_minimum_required(VERSION 3.22)
project(ReplicantTR8S VERSION 3.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ------------------------------------------------------------------------------
# JUCE Dependency via FetchContent (JUCE 8)
# ------------------------------------------------------------------------------
include(FetchContent)

set(JUCE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(JUCE_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
    GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(JUCE)

# ------------------------------------------------------------------------------
# Formats: VST3, AU (macOS only), and Standalone
# ------------------------------------------------------------------------------
set(PLUGIN_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND PLUGIN_FORMATS AU)
endif()

# ------------------------------------------------------------------------------
# Plugin Target Definition
# ------------------------------------------------------------------------------
juce_add_plugin(ReplicantTR8S
    COMPANY_NAME "DanimanX"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD TRUE
    PLUGIN_MANUFACTURER_CODE DnmX
    PLUGIN_CODE TR8S
    FORMATS ${PLUGIN_FORMATS}
    PRODUCT_NAME "ReplicantTR8S"
    DESCRIPTION "1:1 TR-8S Rhythm Performer Clone (ACB 808/909/707/727/606/CR-78 + FM + User Sampler, Firmware v3.00)"
    BUNDLE_ID "com.danimanx.replicanttr8s"
)

# ------------------------------------------------------------------------------
# Source Files
# ------------------------------------------------------------------------------
target_sources(ReplicantTR8S PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h

    # Runtime Firmware Loader (parses user-supplied tr8s_sys_v300.zip or TR8S_UP.bin at runtime)
    Source/Firmware/TR8SFirmwareData.h
    Source/Firmware/TR8SFirmwareLoader.cpp

    # DSP Engine
    Source/DSP/ACBDrumVoice.h
    Source/DSP/ACBDrumVoice.cpp
    Source/DSP/MasterBusProcessor.h
    Source/DSP/MasterBusProcessor.cpp
    Source/DSP/TR8SEngine.h
    Source/DSP/TR8SEngine.cpp

    # GUI Look & Feel + LCD Screen
    Source/GUI/TR8SLookAndFeel.h
    Source/GUI/TR8SLookAndFeel.cpp
    Source/GUI/TR8SDisplayComponent.h
    Source/GUI/TR8SDisplayComponent.cpp
)

# ------------------------------------------------------------------------------
# Compilation & Link Settings
# ------------------------------------------------------------------------------
target_compile_definitions(ReplicantTR8S PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_include_directories(ReplicantTR8S PRIVATE
    Source
    Source/Firmware
    Source/DSP
    Source/GUI
)

target_link_libraries(ReplicantTR8S PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_audio_formats
    juce::juce_dsp
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)

# ------------------------------------------------------------------------------
# UI Snapshot & DSP Verification Tool
# ------------------------------------------------------------------------------
add_executable(generate_snapshot EXCLUDE_FROM_ALL
    tools/generate_snapshot.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Firmware/TR8SFirmwareLoader.cpp
    Source/DSP/ACBDrumVoice.cpp
    Source/DSP/MasterBusProcessor.cpp
    Source/DSP/TR8SEngine.cpp
    Source/GUI/TR8SLookAndFeel.cpp
    Source/GUI/TR8SDisplayComponent.cpp
)

target_compile_definitions(generate_snapshot PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JucePlugin_Name="ReplicantTR8S"
)

target_include_directories(generate_snapshot PRIVATE
    Source
    Source/Firmware
    Source/DSP
    Source/GUI
)

target_link_libraries(generate_snapshot PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_audio_formats
    juce::juce_dsp
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)
