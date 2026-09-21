# Copyright (c) 2026 LG Electronics, Inc.
#
# SPDX-License-Identifier: Apache-2.0

# gtest comes prebuilt from the sysroot rather than from source, so
# webos_use_gtest() (which expects ${WEBOS_INSTALL_SRCDIR}/gtest) is not used.
if(NOT WEBOS_CONFIG_BUILD_TESTS)
    return()
endif()

pkg_check_modules(GTEST REQUIRED gtest_main)

include_directories(${GTEST_INCLUDE_DIRS})
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

file(GLOB TEST_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/Test*.cpp)

webos_add_test(sam_test
    SOURCES ${TEST_SOURCES}
    LIBRARIES ${CMAKE_PROJECT_NAME}-core ${LIBS} ${GTEST_LDFLAGS}
)
