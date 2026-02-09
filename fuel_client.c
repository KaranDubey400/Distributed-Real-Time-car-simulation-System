#include <stdio.h>
#include <unistd.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <math.h>



#define SERVER_PORT 9734
#define CLIENT_FUEL 3

#define FUEL_ENERGY_J_PER_L 34000000.0
#define ENGINE_EFFICIENCY 0.30
#define DT 0.016

#define TANK_CAPACITY 100.0
#define LOW_FUEL_THRESHOLD 10.0



typedef struct {
    int    client_id;
    double throttle;
    double speed;
    int    rpm;
    double power;
    double current_fuel;
} FuelIn;

typedef struct {
    int    client_id;
    double updated_fuel;
    int    no_fuel;
    int    low_fuel;
    int    full_fuel;
} FuelOut;



int main() {
    int sock = socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(SERVER_PORT);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("[FUEL] connect failed");
        return 1;
    }

    
    int client_id = CLIENT_FUEL;
    write(sock, &client_id, sizeof(client_id));

    printf("[FUEL] Connected to server\n");

    while (1) {
        FuelIn in;
        FuelOut out;

        ssize_t n = read(sock, &in, sizeof(in));
        if (n <= 0) {
            printf("[FUEL] Server disconnected\n");
            break;
        }

       
        double fuel_burn = 0.0;

        if (in.power > 0.0 &&
            ENGINE_EFFICIENCY > 0.0 &&
            in.current_fuel > 0.0) {

            fuel_burn =
                (in.power * DT) /
                (ENGINE_EFFICIENCY * FUEL_ENERGY_J_PER_L);
        }

        double updated_fuel = in.current_fuel - fuel_burn;
        if (updated_fuel < 0.0)
            updated_fuel = 0.0;

        out.client_id = CLIENT_FUEL;
        out.updated_fuel = updated_fuel;
        out.no_fuel = (updated_fuel <= 0.0);
        out.low_fuel = (updated_fuel > 0.0 &&
                        updated_fuel <= LOW_FUEL_THRESHOLD);
        out.full_fuel = (updated_fuel >= TANK_CAPACITY);

        write(sock, &out, sizeof(out));

        printf(
            "[FUEL] power=%.1fW burn=%.6fL fuel=%.3fL\n",
            in.power, fuel_burn, updated_fuel
        );
    }

    close(sock);
    return 0;
}