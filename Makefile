CC  := gcc
CXX := g++

WARN_FLAGS := \
	-Wall -Wextra -Wpedantic -Werror \
	-Wformat -Wformat=2 -Wconversion -Wimplicit-fallthrough \
	-Werror=format-security -Wtrampolines \
	-fzero-init-padding-bits=all

COMMONFLAGS := $(WARN_FLAGS) -isystem libs/ -march=x86-64-v3
CFLAGS   := $(COMMONFLAGS) -std=c11
CXXFLAGS := $(COMMONFLAGS) -std=c++20
CXXFLAGS += -nostdlib++ -fno-rtti -fno-exceptions

LDFLAGS := -lm
LDFLAGS += -nostdlib++

SAFETY_CFLAGS := -fhardened -fsanitize=undefined
SAFETY_LDFLAGS := -fhardened -fsanitize=undefined

CSRC   :=
CXXSRC := main.cpp

TARGET := kickstart

ifeq ($(OS),Windows_NT)
  LDFLAGS += -lgdi32
  TARGET  := $(TARGET).exe
endif

OBJ := $(CSRC:.c=.o) $(CXXSRC:.cpp=.o)

.PHONY: all debug safe fast size clean

all: debug

debug: CFLAGS   += -g -Og
debug: CXXFLAGS += -g -Og
debug: LDFLAGS += -g -Og
debug: $(TARGET)

safe: CFLAGS   += -g -O2 $(SAFETY_CFLAGS)
safe: CXXFLAGS += -g -O2 $(SAFETY_CFLAGS)
safe: LDFLAGS  += -g $(SAFETY_LDFLAGS)
safe: $(TARGET)

fast: CFLAGS   += -O3 -flto=auto -DNDEBUG
fast: CXXFLAGS += -O3 -flto=auto -DNDEBUG
fast: LDFLAGS  += -flto=auto
fast: $(TARGET)
	strip $(TARGET)

size: CFLAGS   += -Oz -flto=auto -DNDEBUG
size: CXXFLAGS += -Oz -flto=auto -DNDEBUG
size: LDFLAGS  += -flto=auto
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
	$(RM) $(TARGET) $(OBJ)
