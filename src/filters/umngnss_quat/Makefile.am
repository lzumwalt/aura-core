noinst_LIBRARIES = libumngnss_quat.a

libumngnss_quat_a_SOURCES = \
	umngnss_quat.cxx umngnss_quat.hxx \
	EKF_15state_quat.c \
	globaldefs.h \
	matrix.c matrix.h \
	misc.c misc.h \
	nav_functions.c nav_functions.h nav_interface.h

AM_CPPFLAGS = -I$(VPATH)/.. -I$(VPATH)/../.. @PYTHON_INCLUDES@
