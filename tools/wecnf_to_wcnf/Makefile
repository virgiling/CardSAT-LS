CXX = g++
CXXFLAGS = -O3 -Wall -std=c++11
TARGET = wecnf_to_wcnf
SOURCE = main.cpp

all: $(TARGET)

$(TARGET): $(SOURCE)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SOURCE)

clean:
	rm -f $(TARGET)

.PHONY: all clean
