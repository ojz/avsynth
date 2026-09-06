/* The exe stub: Drone V2 run alone, in its own process. */
#include "dronev2_app.h"

int main(int argc, char **argv)
{
    return app_run(&DRONE_V2, argc, argv);
}
