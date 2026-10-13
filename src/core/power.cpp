#include "power.h"
#include "ota.h"
#include "input.h"
#include "storage.h"
#include "battery.h"
#include "board.h"
#include "display.h"
#include "theme.h"
#include <Arduino.h>
#include <driver/gpio.h>
#include <esp_sleep.h>
#include <esp_task_wdt.h>
#include <esp_arduino_version.h>

static uint32_t idleMs = 10000;  // Settings: 10, 20, 30 or 60 s (NVS sleep_s)

#if UNIDEX_X4
#include <BoardConfig.h>
#include <PowerManager.h>
// Xteink X4: GPIO13 HIGH keeps battery power on (LOW cuts it). The panel has no power switch. Deep sleep can wake
// only on GPIO0-5 going LOW: Power (GPIO3) and the two button ladders (GPIO1, GPIO2), which only Right and Down
// pull low enough. Light sleep can't see the other ladder keys either, so naps there are short (powerNap).
// The pins are the FreeInk SDK's X4 board profile.
static const gpio_num_t PIN_LATCH = (gpio_num_t)BoardConfig::XTEINK_X4.power.latch0;
static const gpio_num_t PIN_EPD_RST = (gpio_num_t)BoardConfig::XTEINK_X4.display.rst;
static const gpio_num_t PIN_EPD_CS = (gpio_num_t)BoardConfig::XTEINK_X4.display.cs;
static const gpio_num_t WAKE_PINS[] = {GPIO_NUM_1, GPIO_NUM_2, (gpio_num_t)BoardConfig::XTEINK_X4.input.power};
static const uint64_t WAKE_MASK = (1ULL << WAKE_PINS[0]) | (1ULL << WAKE_PINS[1]) | (1ULL << WAKE_PINS[2]);
static const uint32_t NAP_MAX_MS = 40;  // a press is at least ~80 ms: still caught, and debounced as usual
#else
static const gpio_num_t PIN_LATCH = GPIO_NUM_17;      // HIGH keeps battery power on; has a pull-down
static const gpio_num_t PIN_AUDIO_PWR = GPIO_NUM_42;  // active LOW; external pull-up keeps it off in sleep
static const gpio_num_t PIN_EPD_PWR = GPIO_NUM_6;     // active LOW
static const gpio_num_t PIN_EPD_RST = GPIO_NUM_9;
static const gpio_num_t PIN_EPD_CS = GPIO_NUM_11;
// Both buttons have external 10k pull-ups, so no internal pull-ups are needed in sleep.
static const gpio_num_t WAKE_PINS[] = {GPIO_NUM_0, GPIO_NUM_18};
static const uint64_t WAKE_MASK = (1ULL << GPIO_NUM_0) | (1ULL << GPIO_NUM_18);
static const uint32_t NAP_MAX_MS = UINT32_MAX;
#endif

static uint32_t lastActivity;
static void deepSleep(uint64_t timerUs);
static volatile int holds;  // powerHold() calls not yet released (from either core)
static portMUX_TYPE holdMux = portMUX_INITIALIZER_UNLOCKED;

void powerHold() {
  portENTER_CRITICAL(&holdMux);
  holds++;
  portEXIT_CRITICAL(&holdMux);
}

void powerRelease() {
  portENTER_CRITICAL(&holdMux);
  if (holds > 0) holds--;
  portEXIT_CRITICAL(&holdMux);
  lastActivity = millis();  // the full idle time from here, not from before the work
}

void powerInit() {
  // Set each level before releasing the hold from the last sleep. A released pin floats, and
  // the pull-ups/downs would then cut battery power or the panel's power (losing its image).
  const struct { gpio_num_t pin; int level; } held[] = {
#if UNIDEX_X4
    {PIN_LATCH, HIGH}, {PIN_EPD_RST, HIGH}, {PIN_EPD_CS, HIGH},
#else
    {PIN_LATCH, HIGH}, {PIN_EPD_PWR, LOW}, {PIN_EPD_RST, HIGH}, {PIN_EPD_CS, HIGH},
#endif
  };
  for (auto &h : held) {
    pinMode(h.pin, OUTPUT);
    digitalWrite(h.pin, h.level);
    gpio_hold_dis(h.pin);
  }
  gpio_deep_sleep_hold_dis();
#if UNIDEX_X4
  BoardConfig::holdPowerRails();  // the SDK's battery latch (GPIO13 HIGH), as its board profile says
#endif
  idleMs = storageGetInt("sleep_s", 10) * 1000;

#if !UNIDEX_X4
  pinMode(PIN_AUDIO_PWR, OUTPUT);
  digitalWrite(PIN_AUDIO_PWR, HIGH);
#endif

  setCpuFrequencyMhz(80);
  lastActivity = millis();
}

bool powerWokeFromSleep() {
  return esp_reset_reason() == ESP_RST_DEEPSLEEP;
}

void powerActivity() {
  lastActivity = millis();
}

void powerSleepIfIdle() {
  // Sleeping with a button held would wake straight away, in a loop.
  if (millis() - lastActivity < idleMs || inputAnyDown() || holds) return;
#if UNIDEX_DEV
  return;  // test build: always awake, so the test tools never find it asleep (USB host detection can drop out)
#endif
  // Stay awake on USB power: a computer is connected, or the charger is holding the battery at 4.2 V.
  // The board can't sense USB power directly (VBUS isn't wired to any GPIO), so a plain charger that
  // has finished charging looks like battery and it sleeps. The voltage is read at most every 10 s.
  static uint32_t lastCheck;
  static bool onUsb;
  if (lastCheck == 0 || millis() - lastCheck > 10000) {
    onUsb = batteryCharging();
    lastCheck = millis();
  }
  if (onUsb) return;
  deepSleep(0);
}

// Keep the panel powered in its own deep sleep (RAM retained), so the first refresh after waking can be
// partial. RST/CS stay HIGH so it isn't woken or selected. A button wakes it (and, if timerUs, the timer).
static void deepSleep(uint64_t timerUs) {
  otaInstallIfReady();  // an update from another X4, downloaded and checked: restarts into it instead (ota.h)
  displayDeepSleep();  // before the holds below freeze the panel's CS
  gpio_hold_en(PIN_LATCH);
#if !UNIDEX_X4
  gpio_hold_en(PIN_EPD_PWR);
#endif
  gpio_hold_en(PIN_EPD_RST);
  gpio_hold_en(PIN_EPD_CS);
  gpio_deep_sleep_hold_en();

  storageEnd();  // unmount the SD card cleanly (it stays powered: no switch on its supply)
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);  // drop powerNap's timer and GPIO sources
#if UNIDEX_X4
  // The SDK arms the wake pins the C3's way and isolates the other pins for the lowest sleep current. The latch,
  // RST and CS holds above stay (it enables the deep-sleep holds too).
  freeink::PowerManager::armWakeOnPins(WAKE_MASK, true);
  if (timerUs) esp_sleep_enable_timer_wakeup(timerUs);
  freeink::PowerManager::powerDownRailsForSleep();  // nothing to switch off on the X4; kept for the SDK's order
  freeink::PowerManager::deepSleep();
#else
  esp_sleep_enable_ext1_wakeup(WAKE_MASK, ESP_EXT1_WAKEUP_ANY_LOW);
  if (timerUs) esp_sleep_enable_timer_wakeup(timerUs);
  esp_deep_sleep_start();
#endif
}

#if UNIDEX_DEV
void powerSleepFor(uint32_t ms) { deepSleep((uint64_t)ms * 1000); }
#endif

static uint32_t wakeWithin;  // 0: nothing asked for

void powerWakeWithin(uint32_t ms) { wakeWithin = wakeWithin ? min(wakeWithin, ms) : ms; }

void powerNap() {
  // Light sleep pauses USB, so skip it while a host is connected (the Mac sync needs the port).
  // Also skip while a button is held: its release and long-press timing need polling.
  const uint32_t idle = millis() - lastActivity;
#if UNIDEX_DEV
  delay(5);  // test build: no light sleep (see powerSleepIfIdle)
  return;
#endif
  if (HWCDC::isPlugged() || inputAnyDown() || holds || idle >= idleMs) {  // light sleep drops WiFi
    delay(5);
    return;
  }
  uint32_t napMs = idleMs - idle;  // wake for the deep-sleep check, or sooner if an app asked
  if (wakeWithin && wakeWithin < napMs) napMs = wakeWithin;
  wakeWithin = 0;
  if (napMs > NAP_MAX_MS) napMs = NAP_MAX_MS;  // the X4's ladder keys are only seen by polling
  esp_sleep_enable_timer_wakeup((uint64_t)napMs * 1000);
  for (gpio_num_t pin : WAKE_PINS) gpio_wakeup_enable(pin, GPIO_INTR_LOW_LEVEL);
  esp_sleep_enable_gpio_wakeup();
  esp_light_sleep_start();  // (the watchdog's timer stops in light sleep too)
  powerAlive();
}

int powerSleepSeconds() { return idleMs / 1000; }

extern TaskHandle_t loopTaskHandle;  // Arduino's main loop task (cores/esp32/main.cpp)
static const uint32_t WATCHDOG_S = 30;

void powerWatchdogBegin() {
#if ESP_ARDUINO_VERSION_MAJOR >= 3  // Arduino core 3 (the X4 build)
  const esp_task_wdt_config_t config = {WATCHDOG_S * 1000, 0, true};  // no idle tasks watched, as before
  esp_task_wdt_reconfigure(&config);
#else
  esp_task_wdt_init(WATCHDOG_S, true);  // already started by the system (5 s, idle task only): this sets 30 s
#endif
  enableLoopWDT();                      // the loop feeds it once per pass
}

void powerAlive() {
  if (xTaskGetCurrentTaskHandle() == loopTaskHandle) esp_task_wdt_reset();  // only the loop task is watched
}

static void drawFlat() {
  drawEmpty("Charge me", "The battery is empty.", "Plug in USB to charge.");
}

static bool flatShown;
bool powerFlatShown() { return flatShown; }

void powerOffIfFlat() {
  static uint32_t lastCheck;
  static bool checked;
  bool &shown = flatShown;
  if (shown || (checked && millis() - lastCheck < 30000)) return;
  checked = true;
  lastCheck = millis();
  if (!batteryEmpty()) return;
  delay(1000);  // a reading can dip under load (a screen refresh): only act if it's still flat a second later
  if (!batteryEmpty()) return;
  shown = true;
  displayShow(drawFlat, true);
#if UNIDEX_DEV
  return;  // test build: stays on, so the test can see the screen
#endif
  gpio_hold_dis(PIN_LATCH);
  digitalWrite(PIN_LATCH, LOW);  // battery power off (on USB the board stays powered, but then it isn't flat)
  delay(2000);
  deepSleep(0);  // still running (PWR held down): sleep until a button
}

void powerSetSleepSeconds(int s) {
  idleMs = s * 1000;
  storagePutInt("sleep_s", s);
}
