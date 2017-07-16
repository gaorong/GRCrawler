TARGET=spider
SOURCES=$(wildcard *.cpp)
OBJS=$(patsubst %.cpp,%.o,$(SOURCES))

CXX:=g++
#OPTIMIZATION?=-O2 -DNDEBUG
OPTIMIZATION?=-O0
WARNINGS=-Wall
#WARNINGS=-Wall -Werror
REAL_CXXFLAGS=$(OPTIMIZATION) $(CXXFLAGS) $(WARNINGS) $(DEBUG) $(PROF) 
REAL_LDFLAGS=$(LDFLAGS) $(PROF) -rdynamic -lstdc++ -Wl,--no-as-needed   -ldl -lpthread -levent -lcrypt



all: $(TARGET)
	@if [ ! -d ../download ]; then mkdir ../download; fi

$(TARGET): $(OBJS)
	$(CXX) -o $@ $(REAL_LDFLAGS) $^

.cpp.o:
	$(CXX) -g -c  $(REAL_CXXFLAGS) $<

clean:
	-rm -rf $(TARGET) $(OBJS)
	-rm -rf ../$(TARGET)

debug:
	$(MAKE) OPTIMIZATION="" DEBUG="-g -ggdb -g3"

gprof:
	$(MAKE) PROF="-pg" 

install:
	-cp -f $(TARGET) ../

.PHONY: all clean debug gprof installls
