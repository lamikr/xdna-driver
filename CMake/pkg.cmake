# SPDX-License-Identifier: Apache-2.0
# Copyright (C) 2023-2024, Advanced Micro Devices, Inc. All rights reserved.

execute_process(
  COMMAND uname -m
  OUTPUT_VARIABLE XDNA_CPACK_ARCH
  OUTPUT_STRIP_TRAILING_WHITESPACE
  )
execute_process(
  COMMAND awk -F= "$1==\"VERSION_ID\" {print $2}" /etc/os-release
  COMMAND tr -d "\""
  OUTPUT_VARIABLE XDNA_CPACK_LINUX_VERSION
  OUTPUT_STRIP_TRAILING_WHITESPACE
  )
execute_process(
  COMMAND awk -F= "$1==\"ID\" {print $2}" /etc/os-release
  COMMAND tr -d "\""
  OUTPUT_VARIABLE XDNA_CPACK_LINUX_FLAVOR
  OUTPUT_STRIP_TRAILING_WHITESPACE
  )
execute_process(
  COMMAND echo ${XRT_VERSION_STRING}
  COMMAND awk -F. "{print $1}"
  OUTPUT_VARIABLE CPACK_PACKAGE_VERSION_MAJOR
  OUTPUT_STRIP_TRAILING_WHITESPACE
  )
execute_process(
  COMMAND echo ${XRT_VERSION_STRING}
  COMMAND awk -F. "{print $2}"
  OUTPUT_VARIABLE CPACK_PACKAGE_VERSION_MINOR
  OUTPUT_STRIP_TRAILING_WHITESPACE
  )
set(CPACK_PACKAGE_VERSION_PATCH ${XRT_PLUGIN_VERSION_PATCH})

set(CPACK_SET_DESTDIR ON)
set(CPACK_COMPONENTS_ALL ${XDNA_COMPONENT})
set(CPACK_GENERATOR "DEB")
set(CPACK_GENERATOR "RPM")
set(CPACK_PACKAGE_VENDOR "AMD Inc")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "XDNA driver plugin for Xilinx RunTime")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/xrt/LICENSE")
set(CPACK_PACKAGE_CONTACT "max.zhen@amd.com")
set(CPACK_PACKAGE_NAME "xrt_plugin")
set(CPACK_DEB_COMPONENT_INSTALL yes)
set(CPACK_RPM_COMPONENT_INSTALL yes)
set(CPACK_PACKAGE_FILE_NAME
  "${CPACK_PACKAGE_NAME}.${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}.${CPACK_PACKAGE_VERSION_PATCH}_${XDNA_CPACK_LINUX_FLAVOR}${XDNA_CPACK_LINUX_VERSION}-${XDNA_CPACK_ARCH}")
math(EXPR next_minor "${CPACK_PACKAGE_VERSION_MINOR} + 1")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "xrt-npu (>= ${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}), xrt-npu (<< ${CPACK_PACKAGE_VERSION_MAJOR}.${next_minor})")
set(CPACK_RPM_PACKAGE_DEPENDS "xrt-npu (>= ${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}), xrt-npu (<< ${CPACK_PACKAGE_VERSION_MAJOR}.${next_minor})")

install(DIRECTORY ${AMDXDNA_BINS_DIR}/firmware/
  DESTINATION /lib/firmware/amdnpu
  COMPONENT ${XDNA_COMPONENT}
  FILES_MATCHING
  PATTERN "*.sbin"
  PATTERN "download_raw" EXCLUDE
  )

install(DIRECTORY ${AMDXDNA_BINS_DIR}/download_raw/xbutil_validate/bins/
  DESTINATION xrt/${XDNA_COMPONENT}/bins
  COMPONENT ${XDNA_COMPONENT}
  FILES_MATCHING
  PATTERN "*.xclbin"
  PATTERN "*.txt"
  )

configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/CMake/config/postinst.in
  ${CMAKE_CURRENT_BINARY_DIR}/package/postinst
  @ONLY
  )
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/CMake/config/prerm.in
  ${CMAKE_CURRENT_BINARY_DIR}/package/prerm
  @ONLY
  )
set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA "${CMAKE_CURRENT_BINARY_DIR}/package/postinst"
set(CPACK_RPM_PACKAGE_CONTROL_EXTRA "${CMAKE_CURRENT_BINARY_DIR}/package/postinst"
  "${CMAKE_CURRENT_BINARY_DIR}/package/prerm")

include(CPack)
