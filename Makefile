CXXFLAGS=-std=c++11 -Wall -Wextra -Werror -g $(EXTRA_CXXFLAGS)
LDFLAGS=$(EXTRA_LDFLAGS)

generator: generator.cpp
		$(CXX) -o generator $(CXXFLAGS) generator.cpp $(LDFLAGS) -ljsoncpp -ltinyxml2

.PHONY: format
format:
		clang-format -i generator.cpp
