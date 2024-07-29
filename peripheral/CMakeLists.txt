# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(link_control)

target_sources(app PRIVATE 
	src/peripheral.c
	src/link_control/link_control.c
	src/link_control/link_control_service.c
)

include_directories(include)
