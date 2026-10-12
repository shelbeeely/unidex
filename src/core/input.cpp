#include "input.h"
#include <Arduino.h>
#include <esp_sleep.h>
#include "board.h"

static const uint32_t DEBOUNCE_MS = 30;  // no bounce seen at this value in STEP 0
static const uint32_t LONG_MS = 300;     // tuned on the device in STEP 2
static const uint32_t RESET_MS = 1000;   // both buttons held this long = restart

struct Button {
  int pin;
  Event shortEv, longEv;
  bool down;
  bool longSent;
  uint32_t changedAt;
};

#if UNIDEX_X4
// Xteink X4, through the FreeInk SDK's InputManager (its ADC ladder ranges for the four front keys on GPIO1 and the
// two side keys on GPIO2, and Power on GPIO3). The keys map onto the two-button scheme: any arrow key (front
// Left/Right, side Up/Down) is A, Confirm and Power are B, and Back is "hold A" straight away. Two keys on the same
// ladder pin can't be told apart when both are down, so the restart is Power (or Confirm) held with a side key.
#include <InputManager.h>

static InputManager keys;
enum : int { KEY_A = -1, KEY_B = -2, KEY_BACK = -3 };

static Button buttons[] = {
  {KEY_A, Event::AShort, Event::ALong, false, false, 0},
  {KEY_B, Event::BShort, Event::BLong, false, false, 0},
  {KEY_BACK, Event::ALong, Event::ALong, false, false, 0},  // one "back" per press, however long
};

static uint8_t state;  // InputManager's raw key bits, read once per poll
static bool key(uint8_t k) { return state & (1 << k); }
static void readKeys() { state = keys.getState(); }

static bool isDown(const Button &b) {
  switch (b.pin) {
    case KEY_A:
      return key(InputManager::BTN_LEFT) || key(InputManager::BTN_RIGHT) || key(InputManager::BTN_UP) ||
             key(InputManager::BTN_DOWN);
    case KEY_B: return key(InputManager::BTN_CONFIRM) || key(InputManager::BTN_POWER);
    default: return key(InputManager::BTN_BACK);
  }
}
static bool bDown() {
  readKeys();
  return isDown(buttons[1]);
}

static void pinsInit() {
  keys.begin();
  readKeys();
}

// Which keys can wake the board (power.cpp): Power, and the ladder keys that pull their pin right down (Right, Down).
static bool wokeBy(const Button &b, uint64_t mask) {
  if (b.pin == KEY_A) return mask & ((1ULL << InputManager::BUTTON_ADC_PIN_1) | (1ULL << InputManager::BUTTON_ADC_PIN_2));
  if (b.pin == KEY_B) return mask & (1ULL << BoardConfig::ACTIVE.input.power);
  return false;
}
static bool buttonWoke() { return esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_GPIO; }
static uint64_t wakeMask() { return esp_sleep_get_gpio_wakeup_status(); }
#else
// Both buttons are active LOW. GPIO0 is a strapping pin, but only at reset, so it's a normal input here.
static Button buttons[] = {
  {0, Event::AShort, Event::ALong, false, false, 0},
  {18, Event::BShort, Event::BLong, false, false, 0},
};
static bool isDown(const Button &b) { return digitalRead(b.pin) == LOW; }
static bool bDown() { return digitalRead(18) == LOW; }
static void pinsInit() {
  for (Button &b : buttons) pinMode(b.pin, INPUT_PULLUP);
}
static bool wokeBy(const Button &b, uint64_t mask) { return mask & (1ULL << b.pin); }
static void readKeys() {}
static bool buttonWoke() { return esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT1; }
static uint64_t wakeMask() { return esp_sleep_get_ext1_wakeup_status(); }
#endif

static Event pending = Event::None;  // a wake tap that was over before we could see it
static bool resetSent;

void inputInit() {
  // After a deep-sleep wake the press that woke us counts as input. After power-on it doesn't:
  // holding PWR to switch on would otherwise open an app.
  const bool buttonWake = buttonWoke();
  const uint64_t wokeMask = buttonWake ? wakeMask() : 0;
  pinsInit();
  for (Button &b : buttons) {
    const bool held = isDown(b);
    b.down = held;
    b.longSent = held && !buttonWake;  // power-on press: its release produces nothing
    b.changedAt = 0;                   // a held wake press started at about boot time
    if (!held && wokeBy(b, wokeMask)) pending = b.shortEv;  // tapped and released during boot
  }
}

#if UNIDEX_DEV
static uint32_t virtualHoldUntil;
void inputVirtualHoldB(uint32_t ms) { virtualHoldUntil = millis() + ms; }
bool inputBHeld() { return bDown() || (int32_t)(virtualHoldUntil - millis()) > 0; }
#else
bool inputBHeld() { return bDown(); }
#endif

bool inputAnyDown() {
  for (Button &b : buttons)
    if (b.down) return true;
  return false;
}

Event inputPoll() {
  if (pending != Event::None) {
    Event e = pending;
    pending = Event::None;
    return e;
  }
  uint32_t now = millis();
  readKeys();
  // Both held: neither gives its own events (no "home" on the way to a restart), and after 3 s: Reset.
  Button &a = buttons[0], &b = buttons[1];
  if (a.down && b.down) {
    a.longSent = b.longSent = true;
    if (!resetSent && now - max(a.changedAt, b.changedAt) >= RESET_MS) {
      resetSent = true;
      return Event::Reset;
    }
  } else {
    resetSent = false;
  }
  for (Button &b : buttons) {
    bool pressed = isDown(b);
    if (pressed != b.down && now - b.changedAt > DEBOUNCE_MS) {
      b.down = pressed;
      b.changedAt = now;
      if (pressed) {
        b.longSent = false;
      } else if (!b.longSent) {
        return b.shortEv;
      }
    }
    // Long fires while still held, so the user knows when to let go.
    if (b.down && !b.longSent && now - b.changedAt >= LONG_MS) {
      b.longSent = true;
      return b.longEv;
    }
  }
  return Event::None;
}
