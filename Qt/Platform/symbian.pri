DEFINES += "BOOST_COMPILER_CONFIG=\"boost/mpl/aux_/config/gcc.hpp\""
QMAKE_CXXFLAGS += -marm -Wno-parentheses -Wno-comment -Wno-unused-local-typedefs

!contains(CONFIG, staticlib) {
	# Executable
	LIBS += -lremconcoreapi -lremconinterfacebase

	# Packaging
	TARGET.UID3 = 0xE0095B1D
	DEPLOYMENT.display_name = PPSSPP
	vendor_deploy.pkg_prerules = "%{\"Qtness\"}" ":\"Qtness\""
	ICON = $$P/assets/icon.svg

	DEPLOYMENT += vendor_deploy
	MMP_RULES += "DEBUGGABLE"

	# 268 MB maximum
	TARGET.EPOCHEAPSIZE = 0x40000 0x10000000
	TARGET.EPOCSTACKSIZE = 0x10000
}
