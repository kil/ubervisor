#
# Copyright (c) 2011 Kilian Klimek <kilian.klimek@googlemail.com>
# All rights reserved.
# 
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 
#   1. Redistributions of source code must retain the above copyright
#      notice, this list of conditions and the following disclaimer.
# 
#   2. Redistributions in binary form must reproduce the above copyright
#      notice, this list of conditions and the following disclaimer in the
#      documentation and/or other materials provided with the distribution.
# 
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDER AND CONTRIBUTORS
# ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT
# HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
cmake_minimum_required(VERSION 2.4)
PROJECT(ubervisor C)
INCLUDE(CheckIncludeFiles)
INCLUDE(CheckLibraryExists)
INCLUDE (CheckFunctionExists)
SET(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)

FIND_PACKAGE(Event REQUIRED)
FIND_PACKAGE(jsonc REQUIRED)

IF(UV_DEBUG)
	MESSAGE(STATUS "debug build")
	ADD_DEFINITIONS("-Wall -Wextra -g -ggdb -DDEBUG -Wunused -Wcast-align
			-Wcast-qual -Wchar-subscripts -Wnested-externs
			-Wpointer-arith -Wbad-function-cast -Wredundant-decls
			-Wshadow -Wstrict-prototypes -Wwrite-strings")
ELSE(UV_DEBUG)
	ADD_DEFINITIONS("-Wall -O2")
ENDIF(UV_DEBUG)
IF(CLANG_ANALYZE)
	ADD_DEFINITIONS("--analyze")
ENDIF(CLANG_ANALYZE)

CHECK_INCLUDE_FILES(sys/queue.h HAVE_SYS_QUEUE_H)
CHECK_FUNCTION_EXISTS(setproctitle HAVE_SETPROCTITLE)

IF(MAN_INSTALL_DIR)
ELSE(MAN_INSTALL_DIR)
	SET(MAN_INSTALL_DIR ${CMAKE_INSTALL_PREFIX}/man/man1)
ENDIF(MAN_INSTALL_DIR)

MESSAGE(STATUS "mandir: " ${MAN_INSTALL_DIR})
FIND_PROGRAM(SPHINX_BUILD sphinx-build)
IF(SPHINX_BUILD)
	MESSAGE(STATUS "sphinx-build: " ${SPHINX_BUILD})
	ADD_CUSTOM_TARGET(man sphinx-build -b man ./doc/source  ./doc/build/man)
	ADD_CUSTOM_TARGET(htmldoc sphinx-build -b html ./doc/source  ./doc/build/html)
	INSTALL(CODE "FILE(GLOB MANPAGES \"doc/build/man/*.1\")"
		CODE "FILE(INSTALL \${MANPAGES} DESTINATION \"${MAN_INSTALL_DIR}\")")
ELSE(SPHINX_BUILD)
	MESSAGE(STATUS "sphinx-build not found. Documentation cannot be build.")
	ADD_CUSTOM_TARGET(man echo sphinx-build was not found by cmake)
	ADD_CUSTOM_TARGET(htmldoc echo sphinx-build was not found by cmake)
ENDIF(SPHINX_BUILD)

CONFIGURE_FILE(cmake-config.h.in
	${PROJECT_BINARY_DIR}/config.h
	@ONLY)

INCLUDE_DIRECTORIES(.
	${EVENT_INCLUDE_DIR}
	${JSON_INCLUDE_DIR})

ADD_EXECUTABLE(ubervisor
	child_config.c client.c cmd_start.c cmd_update.c main.c misc.c server.c
	cmd_get.c cmd_proxy.c subscription.c cmd_subscribe.c process.c)

TARGET_LINK_LIBRARIES(ubervisor
	${EVENT_LIBRARIES}
	${JSON_LIBRARIES})

INSTALL(TARGETS ubervisor DESTINATION bin)
INSTALL(PROGRAMS ubervisor-all DESTINATION bin)
