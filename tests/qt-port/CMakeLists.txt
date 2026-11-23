# Directory-by-directory port of Qt's auto tests. Each subdirectory mirrors one Qt test directory
# and registers one CTest named qt_port_<directory>.
add_subdirectory(qsignalblocker)
add_subdirectory(qmetaenum)
add_subdirectory(qpointer)
add_subdirectory(qmetaproperty)
add_subdirectory(qmetamethod)
add_subdirectory(qproperty)
