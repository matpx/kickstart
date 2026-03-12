CXX := g++
CXXFLAGS := -Wall -Wextra -std=c++20 -nostdlib++ -fno-rtti -fno-exceptions -isystem libs/
LDFLAGS := -nostdlib++ -lm

SRC := \
	main.cpp

TARGET := kickstart

.PHONY: all debug speed size clean

all: debug

debug: CXXFLAGS += -g -O0
debug: $(TARGET)

speed: CXXFLAGS += -O3 -flto=auto -march=x86-64-v3 -DNDEBUG
speed: LDFLAGS += -flto=auto -s
speed: $(TARGET)
	strip $(TARGET)

size: CXXFLAGS += -Oz -flto=auto -DNDEBUG
size: LDFLAGS += -flto=auto -s
size: $(TARGET)
	strip $(TARGET)
	upx -q --best --ultra-brute $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $(SRC)

clean:
	rm -f $(TARGET)
