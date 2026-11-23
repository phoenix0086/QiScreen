#include "tap_tracker.h"
#include <cstdio>
#include <stdexcept>
static void Check(bool value,const char *message) { if(!value)throw std::runtime_error(message); }
int main() {
    try {
        qiscreen::TapTracker tap;
        Check(!tap.Up(1,1,1000),"up without down is not a tap");
        tap.Down(100,100,1000000);Check(tap.Up(103,105,1100000),"a short surface tap opens controls");
        Check(!tap.Up(103,105,1150000),"one gesture produces one tap");
        tap.Down(100,100,2000000);tap.Move(200,200);tap.Move(100,100);
        Check(!tap.Up(100,100,2100000),"dragging back to the origin is not a tap");
        tap.Down(100,100,3000000);Check(!tap.Up(100,100,3500000),"long press is not a tap");
        tap.Down(100,100,4000000);tap.Cancel();Check(!tap.Up(100,100,4100000),"cancel invalidates a gesture");
        puts("PASS native surface tap detection");return 0;
    } catch(const std::exception &e) { fprintf(stderr,"FAIL: %s\n",e.what());return 1; }
}
