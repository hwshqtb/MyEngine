# A5: the audio demo — a real window with two buttons that drive two INDEPENDENT output streams:
# "music" loops the vendored "The Ice Giants.mp3" on its own audio_output_t, and "click" fires sfx_click.wav on
# a second one, so a one-shot overlaps the loop instead of interrupting it (that overlap is the point
# of push-mode streams mixing on one device).
#
# It links ui (widgets + the window) AND audio, which are siblings: the demo is the only place that
# needs both, so neither layer depends on the other.
#
# The two sound files are copied next to this file, so the demo is self-contained: the search below only
# has to bridge "the exe runs in the build directory".
add_executable(audio_demo main.cpp)
target_link_libraries(audio_demo PRIVATE ui audio)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_link_options(audio_demo PRIVATE -Wl,--allow-multiple-definition)
endif()

# Keep the sounds beside the executable as well, so running it from the build tree finds them without
# depending on the working directory.
add_custom_command(TARGET audio_demo POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/The Ice Giants.mp3"
            "${CMAKE_CURRENT_SOURCE_DIR}/sfx_click.wav"
            "$<TARGET_FILE_DIR:audio_demo>"
    VERBATIM)
