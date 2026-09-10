# Petal —— 构建配置
#
# 用法：
#   make            # 构建 Release 版本（默认）
#   make -j8        # 并行构建
#   make debug      # 构建带调试符号的版本
#   make clean      # 清理
#   make run ARGS="-x10 -lnb"   # 构建后运行（需自备数据集）

CXX      ?= g++
CC       ?= gcc
CXXFLAGS ?= -std=c++11 -O2 -Wall -fexceptions
CFLAGS   ?= -O2

TARGET := petal

# 以下文件未被 Code::Blocks 工程（petal.cbp）收录，故不参与构建。
# 它们既没有在 learnerRegistry 中注册，也依赖未启用的条件编译宏：
#   kdbCDRAM.cpp    —— 依赖 dtCatNode 中受 _kdbCDRAM 保护的条件成员，
#                      未定义该宏时无法通过编译（.cbp 只登记了 kdbCDRAM.h）
#   kdbGaussian.cpp —— 同样未接入的实验性学习器（.cbp 只登记了 kdbGaussian.h）
EXCLUDED_SRCS := kdbCDRAM.cpp kdbGaussian.cpp

# 顶层 C++ 源文件 + OPUSMinerCR 子模块
CXX_SRCS := $(filter-out $(EXCLUDED_SRCS),$(wildcard *.cpp)) \
            $(wildcard OPUSMinerCR/*.cpp)
# lbfgs.c 是 C 代码，需要用 C 编译器（对应 .cbp 中的 compilerVar="CC"）
C_SRCS   := $(wildcard *.c)

OBJS := $(CXX_SRCS:.cpp=.o) $(C_SRCS:.c=.o)
DEPS := $(OBJS:.o=.d)

.PHONY: all debug clean run

all: $(TARGET)

debug: CXXFLAGS = -std=c++11 -g -O0 -Wall -fexceptions
debug: CFLAGS   = -g -O0
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
