/*
 * The CAR pages' 2D parts (firmware/s2arcade.c's car_overlay, its static drawing: the file is included) for tools/host/garage_match.c, which compares them with the running original's
 * screenshots: the title, the plate with the name, the top speed and the drive, the set-up list and the panel of its six numbers, the choice.
 */
#include "../s2arcade.c"

/* the page of the championship / time attack in the state `state` (0 cars, 1 set-up, 2 choice) for the car `car` (1 .. 5) with set-up `setup` and the choice `choice` under the cursor,
   at `clock` hundredths of the blinking's clock */
void garage_car_overlay(struct blit_list *l, int team, int car, int state, int setup, int choice, int clock)
{
    struct s2_menu_cfg cfg = { .track = 1, .team = team, .car = car, .laps = 1, .skill = 1, .setup = setup };
    struct car_page cp = { .cfg = &cfg, .zip = s2gfx_zip(), .miles = (s2gfx_line("THIS LAP", 8, "MPH")[0] | 0x20) == 'm', .state = state, .n = 5, .sets = 1, .room = 1, .choice = choice };
    plate_numbers(&cp);
    cp.now = (uint32_t)clock / 2;
    cp.t0 = 0;
    car_overlay(&cp, l);
}
