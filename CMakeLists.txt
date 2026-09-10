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

# 源码目录（新增模块时把目录加进这里即可）
set(PETAL_SRC_DIRS
	src
	src/core
	src/dist
	src/learner
	src/learner/bayes
	src/learner/tree
	src/learner/linear
	src/learner/ensemble
	src/learner/meta
	src/eval
	src/filter
	src/io
	src/utils
	thirdparty
	thirdparty/alglib
	thirdparty/lbfgs
)

# 所有模块目录都作为头文件搜索路径。
# 项目的 #include 一律写成扁平形式（如 #include "xyDist.h"），
# 因此只需把各目录加进 include 路径，不必修改源码中的 include 语句。
foreach(dir ${PETAL_SRC_DIRS})
	file(GLOB _cpp "${CMAKE_CURRENT_SOURCE_DIR}/${dir}/*.cpp")
	file(GLOB _c "${CMAKE_CURRENT_SOURCE_DIR}/${dir}/*.c")
	list(APPEND PETAL_SOURCES ${_cpp} ${_c})
endforeach()

# OPUSMinerCR 位于 thirdparty 下的独立子目录
file(GLOB OPUS_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/OPUSMinerCR/*.cpp")
list(APPEND PETAL_SOURCES ${OPUS_SOURCES})

# 未接入构建的实验性文件，与原始 petal.cbp 的收录范围一致：
#   kdbCDRAM.cpp    依赖 _kdbCDRAM 宏保护的条件成员
#   kdbGaussian.cpp 未接入的实验性学习器
list(FILTER PETAL_SOURCES EXCLUDE REGEX ".*/kdbCDRAM\\.cpp$")
list(FILTER PETAL_SOURCES EXCLUDE REGEX ".*/kdbGaussian\\.cpp$")

add_executable(petal ${PETAL_SOURCES})

# 头文件搜索路径（包含 thirdparty，使 "OPUSMinerCR/xxx.h" 可解析）
set(PETAL_INCLUDE_DIRS "")
foreach(dir ${PETAL_SRC_DIRS})
	list(APPEND PETAL_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/${dir}")
endforeach()
target_include_directories(petal PRIVATE ${PETAL_INCLUDE_DIRS})

# lbfgs.c 为 C 代码，显式按 C 编译
file(GLOB PETAL_C_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/lbfgs/*.c")
set_source_files_properties(${PETAL_C_SOURCES} PROPERTIES LANGUAGE C)
