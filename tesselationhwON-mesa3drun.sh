#!/bin/sh
echo "We need force Opengl 3.3 Compatibility context for run HW tesselation in mesa3d open source" 
MESA_GL_VERSION_OVERRIDE=3.3COMPAT
export MESA_GL_VERSION_OVERRIDE
./PPSSPPSDL
