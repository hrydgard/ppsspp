TEMPLATE = subdirs
SUBDIRS = Native.pro Core.pro Common.pro PPSSPP.pro
CONFIG += ordered
PPSSPP.depends = Native.pro Core.pro Common.pro
