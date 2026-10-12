#include "display.h"
#include <SPI.h>
#include "board.h"
#include "storage.h"

#if !UNIDEX_X4
static const int PIN_SCK = 12, PIN_MOSI = 13, PIN_CS = 11, PIN_DC = 10, PIN_RST = 9, PIN_BUSY = 8;
static const int PIN_EPD_PWR = 6;  // panel power switch, active LOW
// 10 MHz: each refresh sends the image two or three times, so a quicker link means less time awake (a screen
// change 396 -> 378 ms, a game frame 138 -> 120 ms, measured). The panel is rated to 20 MHz; X FTEST tries others.
static SPISettings spi(10000000, MSBFIRST, SPI_MODE0);

#endif

Display display;

void Display::drawPixel(int16_t x, int16_t y, uint16_t color) {
  if (x < 0 || y < 0 || x >= WIDTH || y >= HEIGHT) return;
  const uint8_t bit = 0x80 >> (x & 7);
  uint8_t &b = buf[(y * WIDTH + x) >> 3];
  if (ink(color) == WHITE) b |= bit;
  else b &= ~bit;
}

void Display::fillScreen(uint16_t color) { memset(buf, ink(color) == WHITE ? 0xFF : 0x00, sizeof buf); }

#if UNIDEX_X4
// ---- Xteink X4: a 4.26" 800x480 panel with an SSD1677 controller, used upright (480 across, 800 down, the
// buttons below). The 200x200 screen is drawn at twice the size, each pixel a 2x2 block, in the middle of the
// panel; around it stays paper. The image is scaled as it's sent, so no 48 KB panel buffer is kept in RAM.
// The SD card shares the SPI bus (storage.cpp); every transfer here is one SPI transaction, so they take turns.

static const int PIN_SCK = 8, PIN_MOSI = 10, PIN_MISO = 7, PIN_CS = 21, PIN_DC = 4, PIN_RST = 5, PIN_BUSY = 6;
static const int PANEL_W = 800, PANEL_H = 480;  // the controller's rows (gate lines) are 800 pixels long
static const int SCALE = 2;
static const int LEFT = (PANEL_H - Display::WIDTH * SCALE) / 2;  // 40: the image's place on the upright screen
static const int TOP = (PANEL_W - Display::HEIGHT * SCALE) / 2;  // 200 (a whole number of bytes along a row)
static SPISettings spi(10000000, MSBFIRST, SPI_MODE0);

void displaySetSpiHz(uint32_t hz) { spi = SPISettings(hz, MSBFIRST, SPI_MODE0); }

static void send(uint8_t cmd, const uint8_t *data = nullptr, size_t len = 0) {
  SPI.beginTransaction(spi);
  digitalWrite(PIN_CS, LOW);
  digitalWrite(PIN_DC, LOW);
  SPI.transfer(cmd);
  digitalWrite(PIN_DC, HIGH);
  if (len) SPI.writeBytes(data, len);
  digitalWrite(PIN_CS, HIGH);
  SPI.endTransaction();
}

static void send(uint8_t cmd, std::initializer_list<uint8_t> data) { send(cmd, data.begin(), data.size()); }

static void waitBusy(uint32_t timeoutMs) {
  const uint32_t t0 = millis();
  delay(1);  // BUSY goes high a moment after the command
  while (digitalRead(PIN_BUSY) == HIGH && millis() - t0 < timeoutMs) delay(1);
}

static bool asleep = true;     // in deep sleep: needs a reset before anything else
static bool powered = false;   // the analog side is on
static bool needFull = true;   // RAM doesn't hold what's on screen (cold boot): the next refresh is full
static const bool fastUsed = false;  // no custom game waveform on this panel: frames are ordinary partials
static uint8_t fastFrames, fastRate;  // set by displayFrame(); unused here
static uint32_t busyMs;

// Reset and set the controller up (the X4's own values: booster, gate scan, border). A reset keeps the RAM, so the
// last image is still there for a partial refresh to compare against.
static void wake() {
  if (!asleep) return;
  digitalWrite(PIN_RST, LOW);
  delay(10);
  digitalWrite(PIN_RST, HIGH);
  delay(10);
  send(0x12);  // software reset
  delay(10);
  waitBusy(100);
  send(0x18, {0x80});                                // the built-in temperature sensor picks the waveform
  send(0x0C, {0xAE, 0xC7, 0xC3, 0xC0, 0x80});        // booster soft start
  send(0x01, {(PANEL_H - 1) & 0xFF, (PANEL_H - 1) >> 8, 0x02});  // 480 gate lines, interlaced scan
  send(0x3C, {0x80});                                // border
  asleep = powered = false;
}

// One row as the controller takes it: 800 pixels, 1 = white. Row r is column 479 - r of the upright screen, so
// along it runs one column of the 200x200 image, each pixel twice. `flip` sends the opposite (displayClean).
static void panelRow(int r, uint8_t *out, bool flip) {
  const uint8_t paper = display.inverted ? 0x00 : 0xFF;
  const uint8_t x = flip ? 0xFF : 0x00;
  memset(out, paper ^ x, PANEL_W / 8);
  const int px = PANEL_H - 1 - r;
  if (px < LEFT || px >= LEFT + Display::WIDTH * SCALE) return;
  const int lx = (px - LEFT) / SCALE;
  const uint8_t *col = display.buf + (lx >> 3);
  const uint8_t bit = 0x80 >> (lx & 7);
  uint8_t *o = out + TOP / 8;
  for (int ly = 0; ly < Display::HEIGHT; ly += 4, o++) {  // 4 image pixels -> 8 panel pixels, one byte
    uint8_t b = 0;
    for (int i = 0; i < 4; i++)
      if (col[(ly + i) * (Display::WIDTH / 8)] & bit) b |= 0xC0 >> (2 * i);
    *o = b ^ x;
  }
}

// The whole image into RAM 0x24 (new) or 0x26 (previous, which a partial refresh compares against). The rows go
// in from the last RAM row to the first (the gates are wired in reverse), x counting up.
static void writeRam(uint8_t ram, bool flip = false) {
  send(0x11, {0x01});  // x up, y down
  send(0x44, {0x00, 0x00, (PANEL_W - 1) & 0xFF, (PANEL_W - 1) >> 8});
  send(0x45, {(PANEL_H - 1) & 0xFF, (PANEL_H - 1) >> 8, 0x00, 0x00});
  send(0x4E, {0x00, 0x00});
  send(0x4F, {(PANEL_H - 1) & 0xFF, (PANEL_H - 1) >> 8});
  uint8_t row[PANEL_W / 8];
  SPI.beginTransaction(spi);
  digitalWrite(PIN_CS, LOW);
  digitalWrite(PIN_DC, LOW);
  SPI.transfer(ram);
  digitalWrite(PIN_DC, HIGH);
  for (int r = 0; r < PANEL_H; r++) {
    panelRow(r, row, flip);
    SPI.writeBytes(row, sizeof row);
  }
  digitalWrite(PIN_CS, HIGH);
  SPI.endTransaction();
}

static bool driveAll;  // this partial refresh drives every pixel (displayClean)

// The X4's own update sequences: 0xF7 full (load the temperature and waveform, show, analog off), 0xFC partial
// (mode 2, the analog side left on).
static void update(uint8_t ctrl1, uint8_t control, uint32_t timeoutMs) {
  const uint32_t t0 = millis();
  send(0x21, {ctrl1});  // 0x40: ignore RAM 0x26 (full), 0x00: compare with it (partial)
  send(0x3C, {0xC0});
  send(0x22, {control});
  send(0x20);
  waitBusy(timeoutMs);
  busyMs = millis() - t0;
  powered = !(control & 0x02);
}

static void refreshFull() {
  wake();
  writeRam(0x24);
  writeRam(0x26);
  update(0x40, 0xF7, 5000);
  needFull = false;
}

static void refreshPartial() {
  if (needFull) return refreshFull();
  wake();
  writeRam(0x24);
  if (driveAll) writeRam(0x26, true);
  update(0x00, 0xFC, 2000);
  writeRam(0x26);  // the new image becomes the one the next partial compares against
  writeRam(0x24);
}

// Deep sleep mode 1 keeps the RAM, so after the board wakes the next refresh can still be partial.
static void sleepPanel() {
  if (asleep) return;
  if (powered) {
    send(0x3C, {0x80});
    send(0x22, {0x03});  // analog and clock off
    send(0x20);
    waitBusy(1000);
    powered = false;
  }
  send(0x10, {0x01});
  asleep = true;
}

static void leaveFastFrames() {}

void displayInit(bool initial) {
  pinMode(PIN_CS, OUTPUT);
  digitalWrite(PIN_CS, HIGH);
  pinMode(PIN_DC, OUTPUT);
  digitalWrite(PIN_DC, HIGH);
  pinMode(PIN_RST, OUTPUT);
  digitalWrite(PIN_RST, HIGH);
  pinMode(PIN_BUSY, INPUT);
  SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI, -1);
  asleep = true;
  needFull = initial;
  display.setTextWrap(false);  // with wrap on, measuring a long line reports only its first wrapped piece
  display.inverted = storageGetInt("invert", 0);
}
#else
void displaySetSpiHz(uint32_t hz) { spi = SPISettings(hz, MSBFIRST, SPI_MODE0); }

// ---- The controller (SSD1681): commands over SPI, DC low for the command byte and high for its data. BUSY is
// high while it works.

static void send(uint8_t cmd, const uint8_t *data = nullptr, size_t len = 0) {
  SPI.beginTransaction(spi);
  digitalWrite(PIN_CS, LOW);
  digitalWrite(PIN_DC, LOW);
  SPI.transfer(cmd);
  digitalWrite(PIN_DC, HIGH);
  if (len) SPI.writeBytes(data, len);
  digitalWrite(PIN_CS, HIGH);
  SPI.endTransaction();
}

static void send(uint8_t cmd, std::initializer_list<uint8_t> data) { send(cmd, data.begin(), data.size()); }

static void waitBusy(uint32_t timeoutMs) {
  const uint32_t t0 = millis();
  delay(1);  // BUSY goes high a moment after the command
  while (digitalRead(PIN_BUSY) == HIGH && millis() - t0 < timeoutMs) delay(1);
}

static bool asleep = true;     // in deep sleep: needs a reset before anything else
static bool powered = false;   // the analog side (the voltages that drive the pixels) is on
static bool needFull = true;   // RAM doesn't hold what's on screen (cold boot): the next refresh is full
static bool fastUsed = false;  // our waveform and voltages are loaded, not the panel's own
static uint8_t fastFrames;     // this refresh: 0 = the panel's own waveform; n = ours, the long phase n frames
static uint8_t fastRate;       // frame rate code for ours (higher = faster frames)
static uint32_t busyMs;        // the last refresh: time the panel was busy

// Out of deep sleep (or power-up): reset, then set the panel up. A reset keeps the RAM, so the last image is
// still there to compare against for a partial refresh.
static void wake() {
  if (!asleep) return;
  digitalWrite(PIN_RST, LOW);
  delay(10);
  digitalWrite(PIN_RST, HIGH);
  delay(10);
  waitBusy(100);
  send(0x12);  // software reset: registers back to their defaults, RAM kept
  waitBusy(100);
  send(0x01, {(Display::HEIGHT - 1) & 0xFF, (Display::HEIGHT - 1) >> 8, 0x00});  // 200 gate lines, top to bottom
  send(0x11, {0x03});                                   // RAM address: x then y, both counting up
  send(0x44, {0x00, Display::WIDTH / 8 - 1});           // RAM x range, in bytes
  send(0x45, {0x00, 0x00, (Display::HEIGHT - 1) & 0xFF, (Display::HEIGHT - 1) >> 8});  // RAM y range
  send(0x3C, {0x05});                                   // border: stays white
  send(0x18, {0x80});                                   // the built-in temperature sensor picks the waveform
  asleep = powered = fastUsed = false;
}

// RAM 0x24 is the new image; RAM 0x26 the previous one, which a partial refresh compares against.
static void writeRam(uint8_t ram) {
  send(0x4E, {0x00});
  send(0x4F, {0x00, 0x00});
  send(ram, display.buf, sizeof display.buf);
}

// The opposite of the screen into RAM 0x26: the next partial refresh then sees every pixel as changed and drives
// all of them, which clears traces of animation frames without a full refresh's black flash.
static void writeRamInverted() {
  send(0x4E, {0x00});
  send(0x4F, {0x00, 0x00});
  uint8_t chunk[100];
  SPI.beginTransaction(spi);
  digitalWrite(PIN_CS, LOW);
  digitalWrite(PIN_DC, LOW);
  SPI.transfer(0x26);
  digitalWrite(PIN_DC, HIGH);
  for (size_t at = 0; at < sizeof display.buf; at += sizeof chunk) {
    for (size_t i = 0; i < sizeof chunk; i++) chunk[i] = ~display.buf[at + i];
    SPI.writeBytes(chunk, sizeof chunk);
  }
  digitalWrite(PIN_CS, HIGH);
  SPI.endTransaction();
}

static bool driveAll;  // this partial refresh drives every pixel (displayClean)

// Runs a refresh. The control byte says what the controller does in order: clock on (0x80), analog on (0x40),
// read the temperature (0x20), load the panel's own waveform (0x10), mode 2 = partial (0x08), show the image
// (0x04), analog off (0x02), clock off (0x01).
static void update(uint8_t control, uint32_t timeoutMs) {
  const uint32_t t0 = millis();
  send(0x22, {control});
  send(0x20);
  waitBusy(timeoutMs);
  busyMs = millis() - t0;
  powered = !(control & 0x02);
}

// The partial waveform Waveshare publishes for this panel (1.54" V2): 153 bytes of waveform (voltage per
// phase for each pixel change, then phase lengths in frames, frame rates), then the gate, source and VCOM
// voltages. Game frames use it with the frame rate raised (and the first, long phase fastFrames frames): faster, a
// little more ghosting, which the full refresh after each round clears.
static const uint8_t PARTIAL_WAVE[159] = {
  0x00, 0x40, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // voltages, one row per kind of pixel change
  0x80, 0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0x40, 0x40, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0x00, 0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0x0F, 0, 0, 0, 0, 0, 0,  // group 0: phase A 15 frames (WAVE_LONG_PHASE)
  0x01, 0x01, 0, 0, 0, 0, 0,  // group 1: one frame each of phases A and B
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0x22, 0x22, 0x22, 0x22, 0x22, 0x22,  // frame rates (WAVE_RATE)
  0, 0, 0,
  0x02, 0x17, 0x41, 0xB0, 0x32, 0x28,  // option, gate voltage, source voltages (3), VCOM
};
static const int WAVE_LONG_PHASE = 60, WAVE_RATE = 144;

static void loadFastWave() {
  uint8_t wave[sizeof PARTIAL_WAVE];
  memcpy(wave, PARTIAL_WAVE, sizeof wave);
  wave[WAVE_LONG_PHASE] = fastFrames;
  for (int i = WAVE_RATE; i < WAVE_RATE + 6; i++) wave[i] = fastRate << 4 | fastRate;
  send(0x32, wave, 153);      // waveform
  send(0x3F, {wave[153]});
  send(0x03, {wave[154]});    // gate voltage
  send(0x04, wave + 155, 3);  // source voltages
  send(0x2C, {wave[158]});    // VCOM
  fastUsed = true;
}

static void refreshFull() {
  wake();
  writeRam(0x24);
  writeRam(0x26);  // both the same, so nothing is left over to compare against
  update(0xF7, 5000);
  needFull = false;
}

static void refreshPartial() {
  if (needFull) return refreshFull();
  wake();
  writeRam(0x24);
  if (driveAll) writeRamInverted();
  if (fastFrames) {
    loadFastWave();
    // Mode 2 with the waveform above (no reload from the panel). Powering the analog side up again costs
    // ~90 ms, so only when it's off; it stays on between frames.
    update(powered ? 0x0C : 0xCC, 1000);
  } else {
    update(0xFC, 2000);  // mode 2 with the panel's own waveform; the analog side stays on
  }
  // The new image into both RAMs: the previous one for the next comparison, and the current one again
  // (skipping this second write left the old image showing through on the next refresh).
  writeRam(0x26);
  writeRam(0x24);
}

// Deep sleep: the image stays on the panel with no power. The next refresh resets the controller, which brings
// back its own waveform and voltages.
static void sleepPanel() {
  if (asleep) return;
  if (powered) update(0x83, 1000);  // analog off
  send(0x10, {0x01});               // deep sleep, keeping the RAM
  asleep = true;
  fastUsed = false;
}

// After game frames: the panel switched off and on again, as a restart does. Without it, the panel's own refreshes
// stayed washed out (soft) after a game until the next restart, however often it refreshed. Its RAM goes with the
// power, so the next refresh is full (it is anyway after a game).
static void leaveFastFrames() {
  sleepPanel();
  digitalWrite(PIN_EPD_PWR, HIGH);  // off
  delay(200);
  digitalWrite(PIN_EPD_PWR, LOW);   // on
  delay(10);
  needFull = true;
}

void displayInit(bool initial) {
  pinMode(PIN_EPD_PWR, OUTPUT);
  digitalWrite(PIN_EPD_PWR, LOW);
  if (initial) delay(10);  // let the panel rail settle; after a wake it stayed powered

  pinMode(PIN_CS, OUTPUT);
  digitalWrite(PIN_CS, HIGH);
  pinMode(PIN_DC, OUTPUT);
  digitalWrite(PIN_DC, HIGH);
  pinMode(PIN_RST, OUTPUT);
  digitalWrite(PIN_RST, HIGH);
  pinMode(PIN_BUSY, INPUT);
  SPI.begin(PIN_SCK, -1, PIN_MOSI, PIN_CS);
  asleep = true;
  needFull = initial;
  display.setTextWrap(false);  // with wrap on, measuring a long line reports only its first wrapped piece
  display.inverted = storageGetInt("invert", 0);
}

#endif  // !UNIDEX_X4

void displaySetInverted(bool on) {
  display.inverted = on;
  storagePutInt("invert", on);
}

static const int FULL_EVERY = 10;
// Game frames' waveform: the long phase its full 15 frames, at rate code 7 (the fastest that behaves): ~100 ms on
// the panel, ~140 ms a frame in all (X FTEST). 3 frames (~70 ms a frame) left heavy ghosting within seconds; the
// games keep their speed whatever this costs (they catch up on the steps a slow frame takes).
static const uint8_t FAST_FRAMES = 15, FAST_RATE = 7;
RTC_DATA_ATTR static int partialsSinceFull;  // survives sleep, so the count is honest

static DisplayRefresh last;

DisplayRefresh displayLastRefresh() { return last; }

static void render(void (*draw)(), bool full, char kind) {
  const uint32_t t0 = millis();
  display.fillScreen(WHITE);
  display.setTextColor(BLACK);
  draw();
  if (full) refreshFull();
  else refreshPartial();
  last = {kind, millis() - t0, last.count + 1, busyMs};
}

void displayClean(void (*draw)()) {
  if (fastUsed) leaveFastFrames();  // back to the panel's own waveform, voltages and a clean start
  partialsSinceFull++;
  driveAll = true;
  render(draw, false, 'C');
  driveAll = false;
  sleepPanel();
}

void displayShow(void (*draw)(), bool full) {
  if (fastUsed) leaveFastFrames();  // back to the panel's own waveform, voltages and a clean start
  full = full || partialsSinceFull >= FULL_EVERY;
  partialsSinceFull = full ? 0 : partialsSinceFull + 1;
  render(draw, full, full ? 'F' : 'P');
  sleepPanel();  // e-ink keeps the image
}

static uint8_t frameWave, frameRate = FAST_RATE;  // the waveform displayFrame uses (0: the panel's own)
static uint8_t gameFrames = FAST_FRAMES, gameRate = FAST_RATE;  // what displayFastFrames(true) uses

void displayFrame(void (*draw)()) {
  fastFrames = frameWave, fastRate = frameRate;  // only for this frame: any other refresh gets the panel's own
  render(draw, false, 'f');
  fastFrames = 0;
}

void displayFastFrames(bool on) {
  frameWave = on ? gameFrames : 0;
  frameRate = gameRate;
}

#if UNIDEX_DEV
void displayFastWave(uint8_t frames, uint8_t rate) { frameWave = frames, frameRate = rate; }
void displaySetGameFrames(uint8_t frames, uint8_t rate) {
  gameFrames = frames ? frames : FAST_FRAMES;
  gameRate = rate ? rate : FAST_RATE;
}
#endif

void displayTick(void (*draw)()) {
  if (fastUsed) leaveFastFrames();
  render(draw, false, 'T');
  sleepPanel();
}
