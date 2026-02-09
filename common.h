#ifndef COMMON_H
#define COMMON_H

#include <stdbool.h>
#include <pthread.h>



#define SHM_NAME "/car_sim_shm"



typedef struct {
    pthread_mutex_t lock;   

   
    double throttle;        // 0 .. 1
    double brake;           // 0 .. 1
    double steer;           // -1 .. 1
    bool   reverse;

   
    double speed;           // m/s
    int    gear;            // -1, 0, 1..5
    double heading;         // radians
    double x;               // meters
    double y;               // meters

   
    double rpm;
    double power;           // watts
    double torque;          // Nm

   
    double fuel;            // litres

    
    bool shutdown;          // set true by server on exit

} CarShared;

#endif
