# Relocatable entry point for the installed external-module build environment.
header_root := $(realpath $(dir $(lastword $(MAKEFILE_LIST))))
export KBUILD_OUTPUT := $(header_root)
override LLVM :=
override LLVM_IAS :=
include $(header_root)/.source/Makefile
