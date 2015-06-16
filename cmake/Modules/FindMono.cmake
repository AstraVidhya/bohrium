# - Try to find the mono, mcs, gmcs and gacutil
#
# defines
#
# MONO_FOUND - system has mono, mcs, gmcs and gacutil
# MONO_EXECUTABLE - where to find 'mono'
# XBUILD_EXECUTABLE - where to find 'xbuild'
# MCS_EXECUTABLE - where to find 'mcs'
# GMCS_EXECUTABLE - where to find 'gmcs'
# GACUTIL_EXECUTABLE - where to find 'gacutil'
#
# copyright (c) 2007 Arno Rehn arno@arnorehn.de
# modified to look for xbuild by Kenneth Skovhede 2014
#
# Redistribution and use is allowed according to the terms of the GPL license.

FIND_PROGRAM (MONO_EXECUTABLE mono)
FIND_PROGRAM (GMCS_EXECUTABLE gmcs)
FIND_PROGRAM (MCS_EXECUTABLE mcs)
FIND_PROGRAM (XBUILD_EXECUTABLE xbuild)
FIND_PROGRAM (GACUTIL_EXECUTABLE gacutil)

IF (NOT GMCS_EXECUTABLE AND MCS_EXECUTABLE)
	SET (GMCS_EXECUTABLE ${MCS_EXECUTABLE})
ENDIF(NOT GMCS_EXECUTABLE AND MCS_EXECUTABLE)

SET (MONO_FOUND FALSE CACHE INTERNAL "")

IF (MONO_EXECUTABLE AND GMCS_EXECUTABLE AND XBUILD_EXECUTABLE AND GACUTIL_EXECUTABLE)
    SET (MONO_FOUND TRUE CACHE INTERNAL "")
ENDIF (MONO_EXECUTABLE AND GMCS_EXECUTABLE AND XBUILD_EXECUTABLE AND GACUTIL_EXECUTABLE)

IF (NOT Mono_FIND_QUIETLY)
    MESSAGE(STATUS "Path to mono: ${MONO_EXECUTABLE}")
    MESSAGE(STATUS "Path to (g)mcs: ${GMCS_EXECUTABLE}")
    MESSAGE(STATUS "Path to xbuild: ${XBUILD_EXECUTABLE}")
    MESSAGE(STATUS "Path to gacutil: ${GACUTIL_EXECUTABLE}")
ENDIF (NOT Mono_FIND_QUIETLY)

IF (NOT MONO_FOUND)
    IF (Mono_FIND_REQUIRED)
        MESSAGE(FATAL_ERROR "Could not find one or more of the following programs: mono, gmcs, xbuild, gacutil")
    ENDIF (Mono_FIND_REQUIRED)
ENDIF (NOT MONO_FOUND)

MARK_AS_ADVANCED(MONO_EXECUTABLE GMCS_EXECUTABLE GACUTIL_EXECUTABLE)