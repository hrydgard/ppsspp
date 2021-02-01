# https://code.google.com/p/osgearth-for-android/source/browse/trunk/CMakeModules/FindLibZip.cmake?r=4

# osgEarth - Dynamic map generation toolkit for OpenSceneGraph
# Copyright 2008-2010 Pelican Mapping

# http://osgearth.org
# git://github.com/gwaldron/osgearth

# osgEarth is free software; you can redistribute it and/or modify
# it under the terms of the GNU Lesser General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.

# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU Lesser General Public License for more details.

# You should have received a copy of the GNU Lesser General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>

# Locate libzip
# This module defines
# LIBZIP_LIBRARY
# LIBZIP_FOUND, if false, do not try to link to libzip 
# LIBZIP_INCLUDE_DIR, where to find the headers
#

FIND_PATH(LIBZIP_INCLUDE_DIR zip.h
    $ENV{LIBZIP_DIR}/include
    $ENV{LIBZIP_DIR}
    $ENV{OSGDIR}/include
    $ENV{OSGDIR}
    $ENV{OSG_ROOT}/include
    ~/Library/Frameworks
    /Library/Frameworks
    /usr/local/include
    /usr/include
    /sw/include # Fink
    /opt/local/include # DarwinPorts
    /opt/csw/include # Blastwave
    /opt/include
    [HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Control\\Session\ Manager\\Environment;OSG_ROOT]/include
    /usr/freeware/include
)

FIND_LIBRARY(LIBZIP_LIBRARY 
    NAMES libzip zip
    PATHS
    $ENV{LIBZIP_DIR}/lib
    $ENV{LIBZIP_DIR}
    $ENV{OSGDIR}/lib
    $ENV{OSGDIR}
    $ENV{OSG_ROOT}/lib
    ~/Library/Frameworks
    /Library/Frameworks
    /usr/local/lib
    /usr/lib
    /sw/lib
    /opt/local/lib
    /opt/csw/lib
    /opt/lib
    [HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Control\\Session\ Manager\\Environment;OSG_ROOT]/lib
    /usr/freeware/lib64
)

SET(LIBZIP_FOUND "NO")
IF(LIBZIP_LIBRARY AND LIBZIP_INCLUDE_DIR)
    SET(LIBZIP_FOUND "YES")
ENDIF(LIBZIP_LIBRARY AND LIBZIP_INCLUDE_DIR)

INCLUDE(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(LIBZIP DEFAULT_MSG LIBZIP_LIBRARY LIBZIP_INCLUDE_DIR)
