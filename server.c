#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdbool.h>
#include <signal.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <pthread.h>

#include "common.h"

/* ---------------- CONSTANTS ---------------- */

#define SERVER_PORT 9734
#define BACKLOG 5
#define DT 0.016

#define CLIENT_ENGINE        1
#define CLIENT_TRANSMISSION  2
#define CLIENT_FUEL          3

/* ---------------- SOCKET MESSAGE STRUCTS ---------------- */

/* ENGINE */
typedef struct {
    double speed;
    double fuel;
    int    gear;
    double heading;
    double x;
    double y;
} EngineStateIn;

typedef struct {
    double throttle;
    double brake;
    double steer;
    int    reverse;

    double speed;
    double heading;
    double x;
    double y;

    double rpm;
    double power;
    double torque;
} EngineStateOut;

/* TRANSMISSION */
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

/* FUEL */
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

/* ---------------- GLOBALS ---------------- */

static CarShared *car = NULL;
static int server_fd = -1;
static int engine_fd = -1;
static int trans_fd  = -1;
static int fuel_fd   = -1;

static volatile sig_atomic_t sigint_received = 0;

/* ---------------- SIGNAL HANDLER ---------------- */

void handle_sigint(int sig) {
    (void)sig;
    sigint_received = 1;
}

/* ---------------- SHARED MEMORY INIT ---------------- */

CarShared *init_shared_memory() {
    int shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd < 0) {
        perror("shm_open");
        exit(1);
    }

    ftruncate(shm_fd, sizeof(CarShared));

    CarShared *ptr = mmap(NULL, sizeof(CarShared),
                          PROT_READ | PROT_WRITE,
                          MAP_SHARED, shm_fd, 0);

    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);

    pthread_mutex_init(&ptr->lock, &attr);

    pthread_mutex_lock(&ptr->lock);
    memset(ptr, 0, sizeof(CarShared));
    ptr->fuel = 100.0;
    ptr->gear = 0;
    ptr->shutdown = false;
    pthread_mutex_unlock(&ptr->lock);

    return ptr;
}

/* ---------------- SOCKET SETUP ---------------- */

int setup_server_socket() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        exit(1);
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(SERVER_PORT);

    bind(fd, (struct sockaddr *)&addr, sizeof(addr));
    listen(fd, BACKLOG);

    return fd;
}

/* ---------------- CLIENT ACCEPT ---------------- */

void accept_clients() {
    while (!sigint_received &&
           (engine_fd < 0 || trans_fd < 0 || fuel_fd < 0)) {

        int client_fd = accept(server_fd, NULL, NULL);

        if (client_fd < 0) {
            if (sigint_received)
                break;
            continue;
        }

        int client_id;
        if (read(client_fd, &client_id, sizeof(client_id)) <= 0) {
            close(client_fd);
            continue;
        }

        if (client_id == CLIENT_ENGINE) {
            engine_fd = client_fd;
            printf("Engine client connected\n");
        } else if (client_id == CLIENT_TRANSMISSION) {
            trans_fd = client_fd;
            printf("Transmission client connected\n");
        } else if (client_id == CLIENT_FUEL) {
            fuel_fd = client_fd;
            printf("Fuel client connected\n");
        } else {
            close(client_fd);
        }
    }
}


/* ---------------- MAIN ---------------- */

int main() {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_sigint;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    sigaction(SIGINT, &sa, NULL);


    car = init_shared_memory();
    server_fd = setup_server_socket();

    printf("Server listening on port %d...\n", SERVER_PORT);
    printf("All clients connecting started. Simulation started.\n");

    accept_clients();

    // printf("All clients connected. Simulation started.\n");

    while (!sigint_received) {
        pthread_mutex_lock(&car->lock);
        bool shutdown = car->shutdown;
        pthread_mutex_unlock(&car->lock);

        if (shutdown)
            break;

        /* ---------- ENGINE ---------- */
        EngineStateIn ein;

        pthread_mutex_lock(&car->lock);
        ein.speed   = car->speed;
        ein.fuel    = car->fuel;
        ein.gear    = car->gear;
        ein.heading = car->heading;
        ein.x       = car->x;
        ein.y       = car->y;
        pthread_mutex_unlock(&car->lock);

        write(engine_fd, &ein, sizeof(ein));

        EngineStateOut eout;
        read(engine_fd, &eout, sizeof(eout));

        pthread_mutex_lock(&car->lock);
        car->throttle = eout.throttle;
        car->brake    = eout.brake;
        car->steer    = eout.steer;
        car->reverse  = eout.reverse;

        car->speed   = eout.speed;
        car->heading = eout.heading;
        car->x       = eout.x;
        car->y       = eout.y;

        car->rpm     = eout.rpm;
        car->power   = eout.power;
        car->torque  = eout.torque;


        //checks  
        if (car->speed < 0) car->speed = 0;
        if (car->speed > 60.0) car->speed = 60.0;

        if (car->rpm < 800) car->rpm = 800;
        if (car->rpm > 6500) car->rpm = 6500;
        pthread_mutex_unlock(&car->lock);

        /* ---------- TRANSMISSION ---------- */
        TransmissionIn tin;

        pthread_mutex_lock(&car->lock);
        tin.client_id = CLIENT_TRANSMISSION;
        tin.speed_mps = car->speed;
        tin.gear      = car->gear;
        tin.rpm       = car->rpm;
        tin.reverse   = car->reverse;
        tin.throttle  = car->throttle; 
        pthread_mutex_unlock(&car->lock);

        write(trans_fd, &tin, sizeof(tin));

        TransmissionOut tout;
        read(trans_fd, &tout, sizeof(tout));

        pthread_mutex_lock(&car->lock);
        car->gear = tout.updated_gear;
        pthread_mutex_unlock(&car->lock);

        /* ---------- FUEL ---------- */
        FuelIn fin;

        pthread_mutex_lock(&car->lock);
        fin.client_id   = CLIENT_FUEL;
        fin.throttle    = car->throttle;
        fin.speed       = car->speed;
        fin.rpm         = (int)car->rpm;
        fin.power       = car->power;
        fin.current_fuel = car->fuel;
        pthread_mutex_unlock(&car->lock);

        write(fuel_fd, &fin, sizeof(fin));

        FuelOut fout;
        read(fuel_fd, &fout, sizeof(fout));

        pthread_mutex_lock(&car->lock);
        car->fuel = fout.updated_fuel;
        pthread_mutex_unlock(&car->lock);

        usleep((int)(DT * 1e6));
    }
    printf("\nServer shutting down cleanly...\n");

    /* Notify monitor */
    pthread_mutex_lock(&car->lock);
    car->shutdown = true;
    pthread_mutex_unlock(&car->lock);

    
    if (server_fd >= 0) close(server_fd);
    if (engine_fd >= 0) close(engine_fd);
    if (trans_fd  >= 0) close(trans_fd);
    if (fuel_fd   >= 0) close(fuel_fd);

    shm_unlink(SHM_NAME);


    return 0;
}
