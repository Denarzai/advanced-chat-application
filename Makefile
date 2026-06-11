# CS-3001 Advanced Chat Application
# Build everything:        make
# Build only TCP:          make tcp
# Build only UDP:          make udp
# Remove built binaries:   make clean

CXX = g++
CXXFLAGS = -Wall -pthread

all: tcp udp

tcp: TCP_Version/server TCP_Version/client

udp: UDP_Version/server UDP_Version/client

TCP_Version/server: TCP_Version/server.cpp
	$(CXX) $(CXXFLAGS) $< -o $@

TCP_Version/client: TCP_Version/client.cpp
	$(CXX) $(CXXFLAGS) $< -o $@

UDP_Version/server: UDP_Version/server.cpp
	$(CXX) $(CXXFLAGS) $< -o $@

UDP_Version/client: UDP_Version/client.cpp
	$(CXX) $(CXXFLAGS) $< -o $@

clean:
	rm -f TCP_Version/server TCP_Version/client UDP_Version/server UDP_Version/client
	rm -f TCP_Version/server_log.txt UDP_Version/udp_server_log.txt
	rm -f TCP_Version/received_*

.PHONY: all tcp udp clean
