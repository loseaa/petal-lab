# Petal —— CMake 构建配置
#
# 本仓库同时提供 Makefile；若环境已安装 cmake，也可用它构建：
#   cmake -B build -DCMAKE_BUILD_TYPE=Release
#   cmake --build build -j
#
# 注意：当前开发机上未安装 cmake，因此 Makefile 是首选构建方式。

cmake_minimum_required(VERSION 3.10)
project(petal LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# 顶层 C++ 源文件与 OPUSMinerCR 子模块
file(GLOB PETAL_CXX_SOURCES CONFIGURE_DEPENDS
	"${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/OPUSMinerCR/*.cpp"
)
file(GLOB PETAL_C_SOURCES CONFIGURE_DEPENDS
	"${CMAKE_CURRENT_SOURCE_DIR}/*.c"
)

add_executable(petal ${PETAL_CXX_SOURCES} ${PETAL_C_SOURCES})

target_include_directories(petal PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}"
	"${CMAKE_CURRENT_SOURCE_DIR}/OPUSMinerCR"
)

# lbfgs.c 为 C 代码，显式按 C 编译
set_source_files_properties(${PETAL_C_SOURCES} PROPERTIES LANGUAGE C)
