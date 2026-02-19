#ifndef V_TYPE_MENU_H
#define V_TYPE_MENU_H

enum app_screen_id {
    APP_SCREEN_GAMEPLAY = 0,
    APP_SCREEN_SHIPYARD = 1,
    APP_SCREEN_ACOUSTICS = 2,
    APP_SCREEN_VIDEO = 3,
    APP_SCREEN_PLANETARIUM = 4,
    APP_SCREEN_LEVEL_EDITOR = 5,
    APP_SCREEN_CONTROLS = 6
};

typedef struct menu_state {
    int current;
    int return_screen;
} menu_state;

void menu_init(menu_state* m);
void menu_open(menu_state* m, int screen, int return_screen);
void menu_back(menu_state* m);
int menu_is_screen(const menu_state* m, int screen);
int menu_preferred_return(const menu_state* m);
int menu_is_gameplay(const menu_state* m);

#endif
