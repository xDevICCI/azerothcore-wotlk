# This file is part of the AzerothCore Project. See AUTHORS file for Copyright information
#
# This file is free software; as a special exception the author gives
# unlimited permission to copy and/or distribute it, with or without
# modifications, as long as this notice is preserved.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY, to the extent permitted by law; without even the
# implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

AC_ADD_SCRIPT("${CMAKE_CURRENT_LIST_DIR}/src/soloqueue.cpp")
AC_ADD_SCRIPT("${CMAKE_CURRENT_LIST_DIR}/src/soloqueue_sc.cpp")
AC_ADD_SCRIPT("${CMAKE_CURRENT_LIST_DIR}/src/cs_soloqueue.cpp")

AC_ADD_SCRIPT_LOADER("ArenaSoloQueue" "${CMAKE_CURRENT_LIST_DIR}/src/soloqueue_loader.cpp")

AC_ADD_CONFIG_FILE("${CMAKE_CURRENT_LIST_DIR}/conf/mod_arena_soloqueue.conf.dist")
