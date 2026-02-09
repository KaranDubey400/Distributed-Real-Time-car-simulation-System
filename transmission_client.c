#include <stdio.h>
#include <unistd.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <time.h>
#include <errno.h>
#include <math.h>

/* ---------------- CONSTANTS ---------------- */

#define SERVER_PORT 9734
#define CLIENT_ID 2   // transmission

#define MAX_GEAR 5
#define MIN_GEAR 0
#define REVERSE_GEAR -1

#define IDLE_RPM 900
#define UPSHIFT_RPM 3500
#define DOWNSHIFT_RPM 1500

#define SPEED_EPSILON 0.1
#define GEAR_CHANGE_COOLDOWN 0.5
#define REVERSE_ENGAGE_SPEED 0.2   // m/s (~0.7 km/h)

/* ---------------- SOCKET STRUCTS ---------------- */

typedef struct {
    int    client_id;
    double speed_mps;
    int    gear;
    double rpm;
    int    reverse;
    double throttle;
} TransmissionIn;

typedef struct {
    int client_id;
    int updated_gear;
} TransmissionOut;

/* ---------------- TIME UTILS ---------------- */

double now_seconds() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

/* ---------------- MAIN ---------------- */

int main() {
    int sock = socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(SERVER_PORT);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("Transmission: connect failed");
        return 1;
    }

    printf("[TRANSMISSION] Connected to server\n");

    /* Identify as transmission client */
    int id = CLIENT_ID;
    write(sock, &id, sizeof(id));
    printf("[TRANSMISSION] Sent client ID = %d\n", id);
    fflush(stdout);

    double last_gear_change_time = 0.0;
    int last_reported_gear = -999;

    while (1) {
        TransmissionIn in;
        TransmissionOut out;

        ssize_t n = read(sock, &in, sizeof(in));
        if (n <= 0) {
            printf("[TRANSMISSION] Server disconnected\n");
            break;
        }

        printf(
            "[TRANSMISSION] RX | speed=%.2f m/s gear=%d rpm=%.0f reverse=%d\n",
            in.speed_mps, in.gear, in.rpm, in.reverse
        );

        out.client_id = CLIENT_ID;
        out.updated_gear = in.gear;

        double current_time = now_seconds();

        /* ---------------- REVERSE HANDLING ---------------- */
        if (in.reverse) {
            if (fabs(in.speed_mps) < REVERSE_ENGAGE_SPEED) {
                out.updated_gear = REVERSE_GEAR;
            } else {
                /* Moving → do NOT engage reverse */
                out.updated_gear = MIN_GEAR;  // neutral
            }

            write(sock, &out, sizeof(out));
            continue;
        }
                /* STATIONARY LOGIC */
        if (in.speed_mps < SPEED_EPSILON) {
            if (!in.reverse && in.rpm >= IDLE_RPM && in.throttle > 0.05) {
                out.updated_gear = 1;   // engage first gear
            } else {
                out.updated_gear = MIN_GEAR;
            }
            write(sock, &out, sizeof(out));
            continue;
        }
        /* ---------------- GEAR VALIDATION ---------------- */
        else if (in.gear < REVERSE_GEAR || in.gear > MAX_GEAR) {
            out.updated_gear = MIN_GEAR;
        }
        /* ---------------- COOLDOWN ---------------- */
        else if ((current_time - last_gear_change_time) < GEAR_CHANGE_COOLDOWN) {
            out.updated_gear = in.gear;
        }
        /* ---------------- UPSHIFT ---------------- */
        else if (in.gear > 0 &&
                 in.gear < MAX_GEAR &&
                 in.rpm > UPSHIFT_RPM) {

            out.updated_gear = in.gear + 1;
            last_gear_change_time = current_time;
        }
        /* ---------------- DOWNSHIFT ---------------- */
        else if (in.gear > 1 &&
                 in.rpm < DOWNSHIFT_RPM) {

            out.updated_gear = in.gear - 1;
            last_gear_change_time = current_time;
        }

        /* Log only if gear changed */
        if (out.updated_gear != last_reported_gear) {
            printf(
                "[TRANSMISSION] GEAR DECISION: %d → %d\n",
                in.gear, out.updated_gear
            );
            last_reported_gear = out.updated_gear;
        }

        write(sock, &out, sizeof(out));
        printf("[TRANSMISSION] TX | updated_gear=%d\n", out.updated_gear);
        fflush(stdout);

        usleep(100000); // 100 ms
    }

    close(sock);
    return 0;
}
