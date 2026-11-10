# Relocatable entry point for the installed external-module build environment.
header_root := $(realpath $(dir $(lastword $(MAKEFILE_LIST))))
export KBUILD_OUTPUT := $(header_root)
include $(header_root)/.source/Makefile
