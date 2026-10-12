#include "apps.h"
#include "../core/board.h"

// Launcher order. Adding an app: one new file in src/apps/<name>/ plus one line here.
extern const App timetableApp, badgeApp, dexApp, chooserApp, notesApp, gamesApp, petApp, settingsApp;

#if UNIDEX_X4
// The Xteink X4 has no microphone, so no Notes.
const App *const APPS[] = {&timetableApp, &petApp, &badgeApp, &dexApp, &chooserApp, &gamesApp, &settingsApp};
#else
const App *const APPS[] = {&timetableApp, &notesApp, &petApp, &badgeApp,
                           &dexApp,       &chooserApp, &gamesApp, &settingsApp};
#endif
const int APP_COUNT = sizeof(APPS) / sizeof(APPS[0]);
