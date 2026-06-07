CXX      = g++
CXXFLAGS = -Wall -Wextra -std=c++17 -O2
LIBS     = -lboost_system -lrt -lpthread

TARGET   = barrier_boost
SRC      = main.cpp

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRC) $(LIBS)

clean:
	rm -f $(TARGET)
	# Curata shared memory daca a ramas din rulari anterioare
	rm -f /dev/shm/boost_barrier_shm

run: $(TARGET)
	./$(TARGET) 4 3

.PHONY: all clean run