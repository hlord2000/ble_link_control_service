# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(link_control)

target_sources(app PRIVATE 
	src/link_control/link_control.c
)

target_sources_ifdef(CONFIG_BT_PERIPHERAL app PRIVATE 
	src/peripheral.c
	src/link_control/link_control_service.c
)
target_sources_ifdef(CONFIG_BT_CENTRAL app PRIVATE src/central.c)

include_directories(include)
