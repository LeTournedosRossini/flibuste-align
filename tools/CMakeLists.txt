# Essai comparatif des étireurs (hors ctest : écrit des WAV pour l'écoute).
add_executable(stretch_eval stretch_eval.cpp)
target_link_libraries(stretch_eval PRIVATE align_render)

# Outil en ligne de commande : guide.wav + double.wav -> double recalé. dr_wav (domaine public / MIT-0) pour lire et écrire les WAV.
add_executable(align_cli align_cli.cpp)
target_include_directories(align_cli PRIVATE ${CMAKE_SOURCE_DIR}/third_party)
target_link_libraries(align_cli PRIVATE align_render)
