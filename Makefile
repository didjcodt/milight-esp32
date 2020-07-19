#
# This is a project Makefile. It is assumed the directory this Makefile resides in is a
# project subdirectory.
#

PROJECT_NAME := milight
IDF_PATH ?= /home/kaizen/esp-idf

include $(IDF_PATH)/make/project.mk

PROJECT_SOURCES := $(wildcard main/*.h)
PROJECT_SOURCES += $(wildcard main/*.c)

lint:
	clang-format -i $(PROJECT_SOURCES)
