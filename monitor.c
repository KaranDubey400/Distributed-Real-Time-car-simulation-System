#include <stdio.h>
#include <unistd.h>
#include <math.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <ncurses.h>
#include <time.h>

#include "common.h"



#define SPEED_WARNING_KMPH 90.0
#define TANK_CAPACITY 100.0



double now_seconds() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}


int main() {
   
    int shm_fd = shm_open(SHM_NAME, O_RDONLY, 0666);
    if (shm_fd < 0) {
        perror("shm_open");
        return 1;
    }

    CarShared *car = mmap(NULL, sizeof(CarShared),
                          PROT_READ,
                          MAP_SHARED,
                          shm_fd, 0);

    if (car == MAP_FAILED) {
        perror("mmap");
        return 1;
    }

    /* ---- ncurses init ---- */
    initscr();
    cbreak();
    noecho();
    curs_set(0);
    nodelay(stdscr, TRUE);

    /* ---- stats ---- */
    double total_distance = 0.0;
    double start_time = now_seconds();
    double last_time = start_time;

    while (1) {
        /* ---- snapshot (NO MUTEX) ---- */
        bool shutdown = car->shutdown;

        double speed = car->speed;
        int gear = car->gear;
        double fuel = car->fuel;
        double x = car->x;
        double y = car->y;
        double heading = car->heading;

        double throttle = car->throttle;
        double brake = car->brake;
        double steer = car->steer;
        bool reverse = car->reverse;

        double rpm = car->rpm;
        double power = car->power;
        double torque = car->torque;

        if (shutdown)
            break;

        /* ---- time & distance ---- */
        double current_time = now_seconds();
        double dt = current_time - last_time;
        last_time = current_time;

        total_distance += fabs(speed) * dt;

        /* ---- derived values ---- */
        double speed_kmph = speed * 3.6;
        double fuel_pct = (fuel / TANK_CAPACITY) * 100.0;
        double elapsed = current_time - start_time;
        double avg_speed = (elapsed > 0)
                            ? (total_distance / elapsed) * 3.6
                            : 0;

        const char *mode =
            reverse ? "REVERSE" :
            (gear == 0 ? "NEUTRAL" : "DRIVE");

        /* ---- UI ---- */
        erase();

        mvprintw(1, 2,  "CAR SIMULATION MONITOR");
        mvprintw(2, 2,  "----------------------");

        mvprintw(4, 2,  "Speed      : %6.2f m/s  (%6.2f km/h)", speed, speed_kmph);
        mvprintw(5, 2,  "Gear       : %d", gear);
        mvprintw(6, 2,  "Mode       : %s", mode);
        mvprintw(7, 2,  "RPM        : %.0f", rpm);

        mvprintw(9, 2,  "Throttle   : %.2f", throttle);
        mvprintw(10,2,  "Brake      : %.2f", brake);
        mvprintw(11,2,  "Steer      : %.2f", steer);

        mvprintw(13,2,  "Fuel       : %.2f L (%.1f%%)", fuel, fuel_pct);
        mvprintw(14,2,  "Power      : %.1f W", power);
        mvprintw(15,2,  "Torque     : %.1f Nm", torque);

        mvprintw(17,2,  "Position   : (%.2f , %.2f)", x, y);
        mvprintw(18,2,  "Heading    : %.2f rad (%.1f deg)",
                 heading, heading * 180.0 / M_PI);

        mvprintw(20,2,  "Distance   : %.2f m", total_distance);
        mvprintw(21,2,  "Avg Speed  : %.2f km/h", avg_speed);

        /* ---- warnings ---- */
        if (speed_kmph > SPEED_WARNING_KMPH) {
            attron(A_BOLD);
            mvprintw(23, 2, "OVERSPEED WARNING!");
            attroff(A_BOLD);
        }

        if (fuel <= 0) {
            attron(A_BOLD);
            mvprintw(24, 2, "NO FUEL");
            attroff(A_BOLD);
        } else if (fuel_pct < 10.0) {
            attron(A_BOLD);
            mvprintw(24, 2, "LOW FUEL");
            attroff(A_BOLD);
        }

        refresh();
        usleep(100000);
    }

    endwin();
    return 0;
}
