# mod-anticheat-pqr
# Anti-PQR/Bot Detection Module for AzerothCore

AC_ADD_SCRIPT("${CMAKE_CURRENT_LIST_DIR}/src/mod_anticheat_pqr.cpp")
AC_ADD_SCRIPT_LOADER("AnticheatPqr" "${CMAKE_CURRENT_LIST_DIR}/src/mod_anticheat_pqr_loader.cpp")
AC_ADD_CONFIG_FILE("${CMAKE_CURRENT_LIST_DIR}/conf/mod_anticheat_pqr.conf.dist")
