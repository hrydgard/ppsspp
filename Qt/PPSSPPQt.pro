TEMPLATE = subdirs
SUBDIRS = Native.pro Core.pro GPU.pro Common.pro PPSSPP.pro
CONFIG += ordered
PPSSPP.depends = Native.pro GPU.pro Core.pro Common.pro
