all: server engine transmission fuel monitor

server: server.c common.h
	gcc server.c -o server -pthread

engine: engine_client.c
	gcc engine_client.c -o engine -lncurses -lm

transmission: transmission_client.c
	gcc transmission_client.c -o transmission

fuel: fuel_client.c
	gcc fuel_client.c -o fuel

monitor: monitor.c common.h
	gcc monitor.c -o monitor -lncurses -pthread -lm


clean:
	rm -f server engine transmission fuel monitor
