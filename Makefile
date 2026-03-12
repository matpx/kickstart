CC  := gcc
CXX := g++

COMMONFLAGS := -Wall -Wextra -Werror -isystem libs/ -march=x86-64-v3

CFLAGS := $(COMMONFLAGS) -std=c11
CXXFLAGS := $(COMMONFLAGS) -std=c++20
CXXFLAGS += -nostdlib++ -fno-rtti -fno-exceptions

LDFLAGS := -lm
LDFLAGS += -nostdlib++

CSRC :=
CXXSRC := main.cpp

OBJ := $(CSRC:.c=.o) $(CXXSRC:.cpp=.o)

TARGET := kickstart

.PHONY: all debug speed size clean

all: debug

debug: CFLAGS += -g -O0
debug: CXXFLAGS += -g -O0
debug: $(TARGET)

speed: CFLAGS += -O3 -flto=auto -DNDEBUG
speed: CXXFLAGS += -O3 -flto=auto -DNDEBUG
speed: LDFLAGS += -flto=auto
speed: $(TARGET)
	strip $(TARGET)

size: CFLAGS += -Oz -flto=auto -DNDEBUG
size: CXXFLAGS += -Oz -flto=auto -DNDEBUG
size: LDFLAGS += -flto=auto
size: $(TARGET)
	strip $(TARGET)
	upx -q --best --ultra-brute $(TARGET)

$(TARGET): $(OBJ)
	$(CXX) -o $@ $(OBJ) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -f $(TARGET) $(OBJ)
