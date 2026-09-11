# Petal —— 构建配置
#
# 用法：
#   make            # 构建 Release 版本（默认），产物为 ./petal
#   make -j8        # 并行构建
#   make debug      # 构建带调试符号的版本
#   make clean      # 清理
#   make run ARGS="-x10 -lnb"   # 构建后运行（需自备数据集）

CXX      ?= g++
CC       ?= gcc
CXXFLAGS ?= -std=c++11 -O2 -Wall -fexceptions
CFLAGS   ?= -O2

TARGET := petal

# ---------------------------------------------------------------------------
# 源码目录（新增模块时把目录加进这里即可）
# ---------------------------------------------------------------------------
SRC_DIRS := \
	src \
	src/core \
	src/dist \
	src/learner \
	src/learner/bayes \
	src/learner/tree \
	src/learner/linear \
	src/learner/ensemble \
	src/learner/meta \
	src/eval \
	src/filter \
	src/io \
	src/utils \
	thirdparty \
	thirdparty/alglib \
	thirdparty/lbfgs

# 所有源码目录都作为头文件搜索路径。
# 项目的 #include 一律写成扁平形式（如 #include "xyDist.h"），
# 因此只需要把各模块目录加进 -I，不必修改源码中的 include 语句。
INCLUDES := $(addprefix -I,$(SRC_DIRS))
CXXFLAGS += $(INCLUDES)
CFLAGS   += $(INCLUDES)

# ---------------------------------------------------------------------------
# 源文件
# ---------------------------------------------------------------------------
CXX_SRCS := $(foreach d,$(SRC_DIRS),$(wildcard $(d)/*.cpp)) \
            $(wildcard thirdparty/OPUSMinerCR/*.cpp)
C_SRCS   := $(wildcard thirdparty/lbfgs/*.c)

# 以下文件未接入构建，与原始 petal.cbp 的收录范围保持一致：
#   kdbCDRAM.cpp    —— 依赖 dtCatNode 中受 _kdbCDRAM 宏保护的条件成员，
#                      未定义该宏时无法编译（原工程只登记了 kdbCDRAM.h）
#   kdbGaussian.cpp —— 未接入的实验性学习器（原工程只登记了 kdbGaussian.h）
EXCLUDED_SRCS := \
	src/learner/bayes/kdbCDRAM.cpp \
	src/learner/bayes/kdbGaussian.cpp
CXX_SRCS := $(filter-out $(EXCLUDED_SRCS),$(CXX_SRCS))

OBJS := $(CXX_SRCS:.cpp=.o) $(C_SRCS:.c=.o)
DEPS := $(OBJS:.o=.d)

.PHONY: all debug clean run compile_commands

all: $(TARGET)

# 生成 compile_commands.json，供 clangd（IDE 智能感知）使用。
# SRC_DIRS / CXXFLAGS / EXCLUDED_SRCS 有变动时重新执行。
compile_commands:
	@python3 tools/gen_compile_commands.py

debug: CXXFLAGS = -std=c++11 -g -O0 -Wall -fexceptions $(INCLUDES)
debug: CFLAGS   = -g -O0 $(INCLUDES)
debug: clean $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -MMD -MP -c -o $@ $<

%.o: %.c
	$(CC) $(CFLAGS) -MMD -MP -c -o $@ $<

run: $(TARGET)
	./$(TARGET) $(ARGS)

clean:
	rm -f $(OBJS) $(DEPS) $(TARGET)

# 自动追踪头文件依赖
-include $(DEPS)
