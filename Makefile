CC  := gcc
CXX := g++

COMMONFLAGS := -Wall -Wextra -Werror -march=x86-64-v3

CFLAGS := $(COMMONFLAGS) -std=c11

CXXFLAGS := $(COMMONFLAGS) -std=c++20 -isystem libs/
CXXFLAGS += -nostdlib++ -fno-rtti -fno-exceptions

LDFLAGS := -lm
LDFLAGS += -nostdlib++

SRC := \
	main.cpp

TARGET := kickstart

.PHONY: all debug speed size clean

all: debug

debug: CXXFLAGS += -g -O0
debug: $(TARGET)

speed: CXXFLAGS += -O3 -flto=auto -DNDEBUG
speed: LDFLAGS += -flto=auto
speed: $(TARGET)
	strip $(TARGET)

size: CXXFLAGS += -Oz -flto=auto -DNDEBUG
size: LDFLAGS += -flto=auto
size: $(TARGET)
	strip $(TARGET)
	upx -q --best --ultra-brute $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) -o $@ $(SRC) $(LDFLAGS)

clean:
	rm -f $(TARGET)
