#include "menu.h"

static int menu_screen_valid(int screen) {
    return (screen >= APP_SCREEN_GAMEPLAY && screen <= APP_SCREEN_CONTROLS) ? 1 : 0;
}

void menu_init(menu_state* m) {
    if (!m) {
        return;
    }
    m->current = APP_SCREEN_GAMEPLAY;
    m->return_screen = APP_SCREEN_GAMEPLAY;
}

void menu_open(menu_state* m, int screen, int return_screen) {
    if (!m) {
        return;
    }
    if (!menu_screen_valid(screen)) {
        return;
    }
    if (!menu_screen_valid(return_screen)) {
        return_screen = APP_SCREEN_GAMEPLAY;
    }
    m->current = screen;
    m->return_screen = return_screen;
}

void menu_back(menu_state* m) {
    if (!m) {
        return;
    }
    if (m->current == APP_SCREEN_GAMEPLAY) {
        return;
    }
    if (m->current == APP_SCREEN_SHIPYARD) {
        m->current = APP_SCREEN_GAMEPLAY;
        m->return_screen = APP_SCREEN_GAMEPLAY;
        return;
    }
    if (!menu_screen_valid(m->return_screen)) {
        m->current = APP_SCREEN_GAMEPLAY;
        m->return_screen = APP_SCREEN_GAMEPLAY;
        return;
    }
    m->current = m->return_screen;
    if (m->current == APP_SCREEN_GAMEPLAY) {
        m->return_screen = APP_SCREEN_GAMEPLAY;
    }
}

int menu_is_screen(const menu_state* m, int screen) {
    if (!m || !menu_screen_valid(screen)) {
        return 0;
    }
    return (m->current == screen) ? 1 : 0;
}

int menu_preferred_return(const menu_state* m) {
    if (!m) {
        return APP_SCREEN_GAMEPLAY;
    }
    if (m->current == APP_SCREEN_SHIPYARD || m->return_screen == APP_SCREEN_SHIPYARD) {
        return APP_SCREEN_SHIPYARD;
    }
    return APP_SCREEN_GAMEPLAY;
}

int menu_is_gameplay(const menu_state* m) {
    if (!m) {
        return 1;
    }
    return (m->current == APP_SCREEN_GAMEPLAY) ? 1 : 0;
}
