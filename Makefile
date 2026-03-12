# Project Name
TARGET = DaisyMother32

USE_DAISYSP_LGPL = 1

# Sources
CPP_SOURCES = DaisyMother32.cpp

# Library Locations
LIBDAISY_DIR = ../../libDaisy
DAISYSP_DIR  = ../../DaisySP

# Core location, and generic makefile.
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile
