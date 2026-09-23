# Include host tools even when a sufficiently new system version is available.
# The exported SDK must have its own consistent CMake/Ninja/pkg-config tools.
PACKAGES += host-cmake host-ninja host-pkgconf

# SDK Python serves Meson, wheel tooling and Rev resource preparation. These
# optional modules otherwise probe arbitrary host headers/libraries, adding
# dependencies absent from the SDK source closure. Keep its bundled decimal,
# expat, ctypes and zlib support; Buildroot already controls SSL/bz2/lzma/curses.
HOST_PYTHON3_CONF_ENV += \
	py_cv_module__zstd=n/a \
	py_cv_module__dbm=n/a \
	py_cv_module__gdbm=n/a \
	py_cv_module_readline=n/a
