CC  := gcc
CXX := g++

WARN_COMMON := \
	-Wall -Wextra -Wpedantic -Werror \
	-Wformat -Wformat=2 -Werror=format-security \
	-Wconversion -Wsign-conversion -Wshadow \
	-Wdouble-promotion -Wimplicit-fallthrough \
	-Wnull-dereference -Wcast-align

WARN_CFLAGS   := \
	$(WARN_COMMON) \
	-Wstrict-prototypes -Wmissing-prototypes

WARN_CXXFLAGS := \
	$(WARN_COMMON) \
	-Wold-style-cast -Woverloaded-virtual \
	-Wnon-virtual-dtor -Wsuggest-override

COMMONFLAGS := -isystem libs/ -march=x86-64-v3
CFLAGS   := $(COMMONFLAGS) $(WARN_CFLAGS) -std=c11
CXXFLAGS := $(COMMONFLAGS) $(WARN_CXXFLAGS) -std=c++20
CXXFLAGS += -nostdlib++ -fno-rtti -fno-exceptions

LDFLAGS := -lm
LDFLAGS += -nostdlib++

SAFETY_CFLAGS := \
	-fno-omit-frame-pointer \
	-fstack-clash-protection \
	-fstack-protector-strong \
	-ftrivial-auto-var-init=zero \
	-fcf-protection=full \
	-fstrict-flex-arrays=3 \
	-U_FORTIFY_SOURCE \
	-D_FORTIFY_SOURCE=3 \
	-D_GLIBCXX_ASSERTIONS \
	-fPIE

SAFETY_LDFLAGS := \
	-pie \
	-Wl,-z,nodlopen \
	-Wl,-z,noexecstack \
	-Wl,-z,relro \
	-Wl,-z,now \
	-Wl,--as-needed \
	-Wl,--no-copy-dt-needed-entries

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

debug: CFLAGS   += -g -Og $(SAFETY_CFLAGS)
debug: CXXFLAGS += -g -Og $(SAFETY_CFLAGS)
debug: LDFLAGS += -g -Og $(SAFETY_LDFLAGS)
debug: $(TARGET)

safe: CFLAGS   += -O2 -flto=auto -fsanitize=undefined $(SAFETY_CFLAGS)
safe: CXXFLAGS += -O2 -flto=auto -fsanitize=undefined $(SAFETY_CFLAGS)
safe: LDFLAGS  += -flto=auto -fsanitize=undefined $(SAFETY_LDFLAGS)
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
