/* The exe stub: vsynth run alone, in its own process. The launcher (ROADMAP
 * P6) will host the same VSYNTH spec instead. */
#include "vsynth_app.h"

int main(int argc, char **argv)
{
    return app_run(&VSYNTH, argc, argv);
}
