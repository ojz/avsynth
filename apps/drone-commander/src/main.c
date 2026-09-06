/* The exe stub: Drone Commander run alone, in its own process. The launcher
 * (ROADMAP P6) will host the same DRONE_COMMANDER spec instead. */
#include "drone_app.h"

int main(int argc, char **argv)
{
    return app_run(&DRONE_COMMANDER, argc, argv);
}
