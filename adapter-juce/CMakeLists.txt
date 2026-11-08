# Sillage (nom provisoire) : plugin CLAP effet, cale une doublure sur un guide reçu par l'entrée latérale.
set(SILLAGE_CLAP_ID "com.tournedosrossini.sillage" CACHE STRING "Identifiant CLAP de Sillage")

juce_add_plugin(Sillage
  COMPANY_NAME                "Le Tournedos Rossini"
  PRODUCT_NAME                "Sillage"
  PLUGIN_MANUFACTURER_CODE    Trss
  PLUGIN_CODE                 Sill
  IS_SYNTH                    FALSE
  NEEDS_MIDI_INPUT            FALSE
  FORMATS                     Standalone)   # Standalone = banc d'essai local ; le .clap vient de clap-juce-extensions

clap_juce_extensions_plugin(
  TARGET        Sillage
  CLAP_ID       "${SILLAGE_CLAP_ID}"
  CLAP_FEATURES audio-effect stereo)

target_sources(Sillage PRIVATE
  src/SillageProcessor.cpp
  src/SillageEditor.cpp
  src/SillageLook.cpp
  src/nova/Theme.cpp)
target_include_directories(Sillage PRIVATE src src/nova)

target_compile_definitions(Sillage PUBLIC
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(Sillage
  PRIVATE
    align_render
    juce::juce_audio_utils
    juce::juce_audio_plugin_client
    clap_juce_extensions
  PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# Essai sans interface du processeur (faux hôte) : hors ctest, il demande la boucle de messages de JUCE.
if(ALIGN_BUILD_TESTS)
  juce_add_console_app(SillageTest PRODUCT_NAME "SillageTest")
  target_sources(SillageTest PRIVATE tests/sillage_test.cpp src/SillageProcessor.cpp src/SillageEditor.cpp src/SillageLook.cpp src/nova/Theme.cpp)
  target_include_directories(SillageTest PRIVATE src src/nova ${CMAKE_SOURCE_DIR}/tests)
  target_compile_definitions(SillageTest PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=0 JUCE_MODAL_LOOPS_PERMITTED=1)
  target_link_libraries(SillageTest PRIVATE align_render juce::juce_audio_utils clap_juce_extensions juce::juce_recommended_config_flags)
endif()
