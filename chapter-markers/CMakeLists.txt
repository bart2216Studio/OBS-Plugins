cmake_minimum_required(VERSION 3.16)

project(bart2216-chapter-markers VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(Qt6 COMPONENTS Widgets REQUIRED)

add_library(${PROJECT_NAME} MODULE
	src/plugin-main.cpp
	src/markers.cpp
	src/markers.hpp
	src/dock.cpp
	src/dock.hpp
)
set_target_properties(${PROJECT_NAME} PROPERTIES PREFIX "")

if(WIN32)
	set(OBS_SOURCE_DIR "" CACHE PATH "Sources d'OBS 32.x (zip decompresse)")
	set(OBS_IMPLIB_DIR "${CMAKE_SOURCE_DIR}/implibs" CACHE PATH "Dossier des .lib generes")

	if(NOT OBS_SOURCE_DIR)
		message(FATAL_ERROR "Definissez -DOBS_SOURCE_DIR=<dossier des sources d'OBS>")
	endif()

	find_path(OBS_LIBOBS_INC obs.h PATHS "${OBS_SOURCE_DIR}/libobs" NO_DEFAULT_PATH)
	find_path(OBS_FRONTEND_INC obs-frontend-api.h
		PATHS "${OBS_SOURCE_DIR}/frontend/api"
		      "${OBS_SOURCE_DIR}/UI/obs-frontend-api"
		NO_DEFAULT_PATH)
	find_library(OBS_LIBOBS_LIB obs PATHS "${OBS_IMPLIB_DIR}" NO_DEFAULT_PATH)
	find_library(OBS_FRONTEND_LIB obs-frontend-api PATHS "${OBS_IMPLIB_DIR}" NO_DEFAULT_PATH)

	foreach(v OBS_LIBOBS_INC OBS_FRONTEND_INC OBS_LIBOBS_LIB OBS_FRONTEND_LIB)
		if(NOT ${v})
			message(FATAL_ERROR "${v} introuvable (OBS_SOURCE_DIR / implibs corrects ?)")
		endif()
	endforeach()

	file(WRITE "${CMAKE_BINARY_DIR}/gen/obsconfig.h" "#pragma once\n")

	target_include_directories(${PROJECT_NAME} PRIVATE
		"${OBS_LIBOBS_INC}" "${OBS_FRONTEND_INC}" "${CMAKE_BINARY_DIR}/gen")
	target_link_libraries(${PROJECT_NAME} PRIVATE
		"${OBS_LIBOBS_LIB}" "${OBS_FRONTEND_LIB}" Qt6::Widgets)
	target_compile_definitions(${PROJECT_NAME} PRIVATE NOMINMAX _CRT_SECURE_NO_WARNINGS)
	target_compile_options(${PROJECT_NAME} PRIVATE /utf-8)
else()
	find_package(libobs REQUIRED)
	find_package(obs-frontend-api REQUIRED)
	target_link_libraries(${PROJECT_NAME} PRIVATE
		OBS::libobs OBS::obs-frontend-api Qt6::Widgets)
endif()
