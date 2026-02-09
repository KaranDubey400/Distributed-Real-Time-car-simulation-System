

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ncurses.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <math.h>
#include <stdbool.h>
#include <time.h>

#define SERVER_PORT 9734
#define CLIENT_ID 1

#define PI 3.14159265359
#define MAX_RPM 7000.0
#define IDLE_RPM 900.0
#define MAX_FORWARD_SPEED 100.0
#define MAX_REVERSE_SPEED 5.50

#define ENGINE_OFF_DECEL 3.0
#define BRAKE_DECEL 30.0
#define THROTTLE_INCREMENT 0.05
#define STEER_INCREMENT 0.1

#define STEERING_RATE (20.0 * PI / 180.0)
#define CENTERING_RATE (33.0 * PI / 180.0)
#define HEADING_DEADZONE (0.5 * PI / 180.0)

#define ROLLING_RESISTANCE 100.0
#define DRAG_FORCE 400.0
#define SYSTEM_EFFICIENCY 0.85
#define DRIVELINE_EFFICIENCY 0.95
#define FINAL_DRIVE 3.5
#define WHEEL_RADIUS 0.3
#define MAX_ENGINE_POWER 150000.0 

typedef struct
{
    double speed;
    double fuel;
    int gear;
    double heading;
    double x;
    double y;
} EngineStateIn;

typedef struct
{
    double throttle;
    double brake;
    double steer;
    int reverse;

    double speed;
    double heading;
    double x;
    double y;

    double rpm;
    double power;
    double torque;
} EngineStateOut;

typedef struct
{
    // Controls
    double throttle;
    double brake;
    double steer;
    bool reverse;

    // State
    double speed;
    int gear;
    double heading;
    double x, y;
    double fuel;

    // Engine specific
    bool engine_on;

    // Physics
    double rpm;
    double power;
    double torque;
    double actual_power;

} CarState;

double gear_ratios[] = {0.0, 3.5, 2.0, 1.5, 1.0, 0.8};

CarState car = {0};
int last_key_pressed = 0;

// Key states
bool key_w_held = false;
bool key_s_held = false;
bool key_a_held = false;
bool key_d_held = false;

bool running = true;

void calculate_physics(double dt)
{
    if (!car.engine_on || car.gear == 0)
    {
        car.rpm = car.engine_on ? IDLE_RPM : 0.0;
        car.torque = 0.0;
        car.power = 0.0;
        return;
    }

    /*Calculate RPM from speed */
    double GR = gear_ratios[car.gear];

    if (car.speed > 0.1)
    {
        car.rpm = (car.speed * 60.0 * GR * FINAL_DRIVE) /
                  (2.0 * PI * WHEEL_RADIUS);
    }
    else
    {
        car.rpm = IDLE_RPM;
    }

    /* clamp RPM */
    if (car.rpm < IDLE_RPM)
        car.rpm = IDLE_RPM;
    if (car.rpm > MAX_RPM)
        car.rpm = MAX_RPM;

    double peak_torque = 250.0; 
    double peak_rpm = 3500.0;   

    double torque_factor;

    if (car.rpm <= peak_rpm)
    {
        /* Torque rises from idle to peak RPM */
        torque_factor = car.rpm / peak_rpm;
    }
    else
    {
        /* Torque falls after peak RPM */
        torque_factor = (MAX_RPM - car.rpm) /
                        (MAX_RPM - peak_rpm);
    }

    if (torque_factor < 0.0)
        torque_factor = 0.0;

    car.torque = peak_torque * torque_factor * car.throttle;

 

    car.power = (car.torque * car.rpm * 2.0 * PI) / 60.0;

    /* clamp engine power */
    if (car.power > MAX_ENGINE_POWER)
        car.power = MAX_ENGINE_POWER;
}

void update_speed(double dt)
{
    bool braking = (car.brake > 0.0);

    if (!car.engine_on)
    {
        // engine off
        if (car.speed > 0.0)
        {
            car.speed -= ENGINE_OFF_DECEL * dt;
            if (car.speed < 0.0)
                car.speed = 0.0;
        }
        car.throttle = 0.0;
    }
    else if (braking)
    {
        // Braking
        if (car.speed > 0.0)
        {
            car.speed -= BRAKE_DECEL * dt;
            if (car.speed < 0.0)
                car.speed = 0.0;
        }
    }
    else if (car.fuel > 0.0 && car.throttle > 0.0)
    {
        // Accelerating
        double acceleration = car.throttle * 10.0;
        car.speed += acceleration * dt;

        // Apply resistance
        double resistance_decel = (ROLLING_RESISTANCE + DRAG_FORCE) * car.speed / 5000.0;
        car.speed -= resistance_decel * dt;

        // Cap speed
        double max_speed = car.reverse ? MAX_REVERSE_SPEED : MAX_FORWARD_SPEED;
        if (car.speed > max_speed)
            car.speed = max_speed;
    }
    else
    {
        // Natural deceleration
        if (car.speed > 0.0)
        {
            car.speed -= 2.0 * dt;
            if (car.speed < 0.0)
                car.speed = 0.0;
        }
    }
}

void update_heading(double dt)
{
    if (fabs(car.speed) > 0.1)
    {
        if (car.steer != 0)
        {
            car.heading += car.steer * STEERING_RATE * dt;

            if (car.heading > PI)
                car.heading -= 2.0 * PI;
            if (car.heading < -PI)
                car.heading += 2.0 * PI;
        }
        else
        {
            if (fabs(car.heading) > HEADING_DEADZONE)
            {
                double center_dir = (car.heading > 0) ? -1.0 : 1.0;
                car.heading += center_dir * CENTERING_RATE * dt;
            }
            else
            {
                car.heading = 0.0;
            }
        }
    }
}

void update_position(double dt)
{
    double effective_speed = car.reverse ? -car.speed : car.speed;

    car.y += effective_speed * cos(car.heading) * dt;
    car.x += effective_speed * sin(car.heading) * dt;
}

void handle_input()
{
    int ch = getch();

    if (ch != ERR)
    {
        last_key_pressed = ch;

        switch (ch)
        {
        case 'e':
        case 'E':
            car.engine_on = !car.engine_on;
            if (!car.engine_on)
            {
                car.throttle = 0.0;
                key_w_held = false;
            }
            break;

        case 'r':
        case 'R':
            if (car.speed < 0.1)
            {
                car.reverse = !car.reverse;
            }
            break;

        case KEY_UP:
        case 'w':
        case 'W':
            key_w_held = true;
            break;

        case KEY_DOWN:
        case 's':
        case 'S':
            key_s_held = true;
            break;

        case KEY_LEFT:
        case 'a':
        case 'A':
            key_a_held = true;
            break;

        case KEY_RIGHT:
        case 'd':
        case 'D':
            key_d_held = true;
            break;

        case 'q':
        case 'Q':
            running = false;
            break;
        }
    }

    if (key_w_held && car.engine_on && car.fuel > 0.0)
    {
        car.throttle += THROTTLE_INCREMENT;
        if (car.throttle > 1.0)
            car.throttle = 1.0;
        car.brake = 0.0;
    }
    else if (car.throttle > 0.0)
    {
        car.throttle -= THROTTLE_INCREMENT * 2.0;
        if (car.throttle < 0.0)
            car.throttle = 0.0;
    }

    if (key_s_held)
    {
        car.brake = 1.0;
        car.throttle = 0.0;
    }
    else
    {
        car.brake = 0.0;
    }

    if (key_a_held && car.speed > 0.1)
    {
        car.steer -= STEER_INCREMENT;
        if (car.steer < -1.0)
            car.steer = -1.0;
    }

    if (key_d_held && car.speed > 0.1)
    {
        car.steer += STEER_INCREMENT;
        if (car.steer > 1.0)
            car.steer = 1.0;
    }

    if (!key_a_held && !key_d_held)
    {
        if (car.steer > 0.01)
        {
            car.steer -= STEER_INCREMENT * 0.5;
            if (car.steer < 0.0)
                car.steer = 0.0;
        }
        else if (car.steer < -0.01)
        {
            car.steer += STEER_INCREMENT * 0.5;
            if (car.steer > 0.0)
                car.steer = 0.0;
        }
    }

    static int frame_count = 0;
    frame_count++;
    if (frame_count > 3)
    {
        key_w_held = false;
        key_s_held = false;
        key_a_held = false;
        key_d_held = false;
        frame_count = 0;
    }
}

void update_display()
{
    // clear();
    erase();

    attron(A_BOLD);
    mvprintw(0, 0, "-- ENGINE CLIENT - Team 1 --");
    attroff(A_BOLD);

    mvprintw(2, 0, "Engine: %s", car.engine_on ? "ON" : "OFF");

    mvprintw(4, 0, "Controls:");
    mvprintw(5, 2, "Throttle: %.1f%%", car.throttle * 100.0);
    mvprintw(6, 2, "Brake:    %.1f%%", car.brake * 100.0);
    mvprintw(7, 2, "Steer:    %.2f", car.steer);

    
    mvprintw(10, 2, "Speed:     %.2f m/s (%.1f km/h)", car.speed, car.speed * 3.6);
    mvprintw(11, 2, "Gear:      %d %s", car.gear, car.gear == 0 ? "(N)" : "");
    mvprintw(12, 2, "Direction: %s", car.reverse ? "REVERSE" : "FORWARD");
    mvprintw(13, 2, "Fuel:      %.2f L", car.fuel);
    mvprintw(14, 2, "Heading:   %.2f deg", car.heading * 180.0 / PI);

    mvprintw(16, 0, "Position:");
    mvprintw(17, 2, "X: %.2f m", car.x);
    mvprintw(18, 2, "Y: %.2f m", car.y);

    if (car.rpm >= MAX_RPM)
    {
        attron(A_BOLD | A_BLINK);
        mvprintw(23, 2, "RPM:      %.0f *** REDLINE ***", car.rpm);
        attroff(A_BOLD | A_BLINK);
    }
    else
    {
        mvprintw(23, 2, "RPM:      %.0f", car.rpm);
    }
  

    mvprintw(25, 0, "Keys:");
    mvprintw(26, 2, "E: Toggle Engine | R: Reverse (when stopped)");
    mvprintw(27, 2, "W/UP: Throttle | S/DOWN: Brake");
    mvprintw(28, 2, "A/LEFT: Steer Left | D/RIGHT: Steer Right | Q: Quit");

    refresh();
}

int main()
{

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
    {
        perror("socket");
        return 1;
    }

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(SERVER_PORT);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        perror("connect");
        return 1;
    }

    printf("[ENGINE] Connected to server\n");

    int id = CLIENT_ID;
    write(sock, &id, sizeof(id));
    printf("[ENGINE] Sent client ID = %d\n", id);

    initscr();
    cbreak();
    noecho();
    nodelay(stdscr, TRUE);
    keypad(stdscr, TRUE);
    curs_set(0);

    car.engine_on = false;
    car.fuel = 100.0;

    struct timespec last_time, current_time;
    clock_gettime(CLOCK_MONOTONIC, &last_time);

    while (running)
    {

        EngineStateIn in;
        ssize_t n = read(sock, &in, sizeof(in));
        if (n <= 0)
        {
            mvprintw(30, 0, "Server disconnected");
            refresh();
            sleep(2);
            break;
        }

        // Update from server
        car.speed = in.speed;
        car.fuel = in.fuel;
        car.gear = in.gear;
        car.heading = in.heading;
        car.x = in.x;
        car.y = in.y;

        // Calculate delta time
        clock_gettime(CLOCK_MONOTONIC, &current_time);
        double dt = (current_time.tv_sec - last_time.tv_sec) +
                    (current_time.tv_nsec - last_time.tv_nsec) / 1e9;
        last_time = current_time;

        // Handle input
        handle_input();

        // Update physics
        update_speed(dt);
        calculate_physics(dt);
        update_heading(dt);
        update_position(dt);

        // Send back to server
        EngineStateOut out;
        out.throttle = car.throttle;
        out.brake = car.brake;
        out.steer = car.steer;
        out.reverse = car.reverse ? 1 : 0;

        out.speed = car.speed;
        out.heading = car.heading;
        out.x = car.x;
        out.y = car.y;

        out.rpm = car.rpm;
        out.power = car.power;
        out.torque = car.torque;

        write(sock, &out, sizeof(out));

        // Update display
        update_display();

        usleep(16666); // ~60 FPS
    }

    // Cleanup
    endwin();
    close(sock);

    printf("[ENGINE] Shut down\n");
    return 0;
}
