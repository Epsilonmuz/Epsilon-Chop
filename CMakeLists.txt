cmake_minimum_required(VERSION 3.22)
project(EpsilonChop VERSION 0.5.0 LANGUAGES C CXX)

set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version")

add_subdirectory(JUCE EXCLUDE_FROM_ALL)

set(EPSILON_CHOP_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND EPSILON_CHOP_FORMATS AU)
endif()

juce_add_plugin(EpsilonChop
    VERSION 0.5.0
    COMPANY_NAME "Epsilon Sounds"
    BUNDLE_ID com.epsilonsounds.chop
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE EpsS
    PLUGIN_CODE EpCh
    FORMATS ${EPSILON_CHOP_FORMATS}
    VST3_CATEGORIES Instrument Sampler
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    PRODUCT_NAME "Epsilon Chop"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Resources/AppIcon.png"
)
juce_generate_juce_header(EpsilonChop)

juce_add_binary_data(EpsilonChopData SOURCES
    Resources/EpsilonDial-Regular.ttf
    Resources/EpsilonDial-Bold.ttf
)

target_sources(EpsilonChop PRIVATE
    Source/ChopEngine.cpp
    Source/SampleAnalysis.cpp
    Source/Theme.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
)
target_compile_features(EpsilonChop PRIVATE cxx_std_17)
target_compile_definitions(EpsilonChop PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_JACK=0
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
)
target_link_libraries(EpsilonChop
    PRIVATE
        EpsilonChopData
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

option(EPSILON_CHOP_BUILD_TESTS "Build deterministic DSP and processor tests" OFF)
if(EPSILON_CHOP_BUILD_TESTS)
    enable_testing()
    add_executable(EpsilonChopEngineTests Tests/EngineTests.cpp Source/ChopEngine.cpp)
    target_include_directories(EpsilonChopEngineTests PRIVATE Source)
    target_compile_features(EpsilonChopEngineTests PRIVATE cxx_std_17)
    add_test(NAME EpsilonChopEngine COMMAND EpsilonChopEngineTests)

    add_executable(EpsilonChopProcessorTests Tests/ProcessorTests.cpp)
    target_compile_features(EpsilonChopProcessorTests PRIVATE cxx_std_17)
    target_include_directories(EpsilonChopProcessorTests PRIVATE Source JUCE/modules
        "${CMAKE_CURRENT_BINARY_DIR}/EpsilonChop_artefacts/JuceLibraryCode"
        "${CMAKE_CURRENT_BINARY_DIR}/juce_binarydata_EpsilonChopData/JuceLibraryCode")
    target_link_libraries(EpsilonChopProcessorTests PRIVATE EpsilonChop)
    add_test(NAME EpsilonChopProcessor COMMAND EpsilonChopProcessorTests)
endif()
