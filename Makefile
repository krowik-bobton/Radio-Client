CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2
LDFLAGS = -lpthread -lssl -lcrypto

TARGET = sikradio
OBJS = main.o consumer.o connection_handler.o client.o parser.o

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS) $(LDFLAGS)

main.o: main.cpp shared_state.h consumer.h connection_handler.h client.h err.h parser.h radio_connection.h
	$(CXX) $(CXXFLAGS) -c main.cpp -o main.o

consumer.o: consumer.cpp consumer.h shared_state.h
	$(CXX) $(CXXFLAGS) -c consumer.cpp -o consumer.o

connection_handler.o: connection_handler.cpp connection_handler.h shared_state.h client.h radio_connection.h err.h parser.h
	$(CXX) $(CXXFLAGS) -c connection_handler.cpp -o connection_handler.o

client.o: client.cpp client.h
	$(CXX) $(CXXFLAGS) -c client.cpp -o client.o

parser.o: parser.cpp parser.h client.h
	$(CXX) $(CXXFLAGS) -c parser.cpp -o parser.o

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean