################################################################################
#
# glslang
#
################################################################################

DATAPUMP_GLSLANG_VERSION = vulkan-sdk-1.4.350.0
DATAPUMP_GLSLANG_SITE = https://github.com/KhronosGroup/glslang/archive
DATAPUMP_GLSLANG_SOURCE = $(DATAPUMP_GLSLANG_VERSION).tar.gz
DATAPUMP_GLSLANG_LICENSE = BSD-3-Clause, BSD-2-Clause, MIT, Apache-2.0, GPL-3.0+ with Bison exception, NVIDIA Software License
DATAPUMP_GLSLANG_LICENSE_FILES = LICENSE.txt
DATAPUMP_GLSLANG_INSTALL_STAGING = YES
DATAPUMP_GLSLANG_DEPENDENCIES = spirv-headers spirv-tools
DATAPUMP_GLSLANG_CONF_OPTS = \
	-DBUILD_EXTERNAL=OFF \
	-DBUILD_SHARED_LIBS=OFF \
	-DGLSLANG_TESTS=OFF \
	-DENABLE_GLSLANG_BINARIES=OFF \
	-DENABLE_OPT=ON \
	-DALLOW_EXTERNAL_SPIRV_TOOLS=ON

$(eval $(cmake-package))
