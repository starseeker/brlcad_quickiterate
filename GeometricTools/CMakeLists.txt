cmake_minimum_required(VERSION 3.8)

project(GTE)

file(GLOB GTE_MFILES "${CMAKE_SOURCE_DIR}/GTE/Mathematics/*")

install(FILES ${GTE_MFILES} DESTINATION GTE/Mathematics)

