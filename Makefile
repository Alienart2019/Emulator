CXX      ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2
SRC      := main.cpp util.cpp registers.cpp stack.cpp parser.cpp emulator.cpp
OBJ      := $(SRC:.cpp=.o)

emulator: $(OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJ)

%.o: %.cpp $(wildcard *.h)
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) emulator

.PHONY: clean
