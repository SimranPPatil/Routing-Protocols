all: ls

ls: ls_main.cpp ls_monitor_neighbors.cpp
	g++ -pthread -std=c++11 -o ls_router ls_main.cpp ls_monitor_neighbors.cpp

.PHONY: clean
clean:
	rm ls_router
