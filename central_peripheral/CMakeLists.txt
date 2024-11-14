# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(link_control_central_peripheral)

target_sources(app PRIVATE 
	src/central_peripheral.c
	${CMAKE_CURRENT_SOURCE_DIR}/../common/src/link_control/link_control.c
	${CMAKE_CURRENT_SOURCE_DIR}/../common/src/link_control/link_control_service.c
	${CMAKE_CURRENT_SOURCE_DIR}/../common/src/conn_adv_pair/conn_adv_pair.c
)

target_include_directories(app PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/../common/include
)
