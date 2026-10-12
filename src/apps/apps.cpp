#include "apps.h"
#include "../core/board.h"

// Launcher order. Adding an app: one new file in src/apps/<name>/ plus one line here.
extern const App timetableApp, badgeApp, dexApp, chooserApp, notesApp, gamesApp, petApp, houseApp, settingsApp;

#if UNIDEX_X4
// The Xteink X4 has no microphone, so no Notes. It leads with the household: the Pets and the House.
const App *const APPS[] = {&petApp,   &houseApp,   &timetableApp, &badgeApp,
                           &dexApp,   &chooserApp, &gamesApp,     &settingsApp};
#else
const App *const APPS[] = {&timetableApp, &notesApp,   &petApp,   &houseApp, &badgeApp,
                           &dexApp,       &chooserApp, &gamesApp, &settingsApp};
#endif
const int APP_COUNT = sizeof(APPS) / sizeof(APPS[0]);
