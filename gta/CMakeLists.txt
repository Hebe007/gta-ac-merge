# GtaAcLink.asi - loaded by ScriptHookV. Windows + MSVC only.
add_library(GtaAcLink SHARED src/main.cpp src/game_shv.cpp src/shm_win.cpp)
target_include_directories(GtaAcLink PRIVATE "${SHV_INC}")
target_link_libraries(GtaAcLink PRIVATE gtalogic "${SHV_LIB}")
target_compile_definitions(GtaAcLink PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
set_target_properties(GtaAcLink PROPERTIES SUFFIX ".asi")
