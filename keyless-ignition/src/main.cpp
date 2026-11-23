// Keyless ignition: ESP32-C3 SuperMini + RC522 RFID + 1.8" ST7735 + rotary encoder
//
// Flow:  LOCKED --tap card--> ARMED (60 s) --press START--> IGNITION ON
//        IGNITION ON --brake/clutch + hold START--> CRANKING (max 4 s) --> RUNNING
//        RUNNING / IGNITION ON --hold START 2 s--> OFF (back to ARMED for 60 s)
//
// The relays are wired IN PARALLEL with the factory key switch, so the original
// key keeps working as a backup. See README.md for wiring and safety notes.

#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <MFRC522.h>
#include <Preferences.h>
#include "driver/gpio.h"
#include "esp_system.h"
#include "esp_attr.h"
#include "esp_timer.h"
#include "soc/gpio_reg.h"

// ---------------------------------------------------------------- pins
// Shared SPI bus (ESP32-C3 Arduino defaults)
constexpr int PIN_SCK = 4;
constexpr int PIN_MISO = 5;
constexpr int PIN_MOSI = 6;
// Display. Its RESET line is tied to 3.3 V; the driver does a software reset.
constexpr int PIN_TFT_CS = 10;
constexpr int PIN_TFT_DC = 1;
// RFID. Its RST line is tied to 3.3 V; the library does a software reset.
constexpr int PIN_RFID_SS = 7;
// Rotary encoder + push button (the button doubles as START/STOP)
constexpr int PIN_ENC_A = 20;
constexpr int PIN_ENC_B = 21;
constexpr int PIN_BUTTON = 9;  // active LOW. Also the BOOT button.
// Relay drivers (active HIGH, via logic-level MOSFET, 10k pulldown on each gate)
constexpr int PIN_IGN = 0;    // was LCD RESET in the original wiring
constexpr int PIN_START = 3;  // was RFID RST in the original wiring
// Inputs through PC817 optocouplers (LOW = active)
constexpr int PIN_INTERLOCK = 2;     // brake or clutch pedal pressed
constexpr int PIN_ENGINE_SENSE = 8;  // engine running (optional)

// ---------------------------------------------------------------- settings
constexpr bool REQUIRE_INTERLOCK = true;   // brake/clutch must be pressed to crank
constexpr bool ENGINE_SENSE_ENABLED = false;  // set true once GPIO8 sense is wired
constexpr uint32_t ARM_WINDOW_MS = 60000;   // how long a card tap stays valid
constexpr uint32_t PRIME_MS = 1500;         // fuel pump prime before first crank
constexpr uint32_t MAX_CRANK_MS = 4000;     // hard limit per crank attempt
constexpr uint32_t CRANK_COOLDOWN_MS = 8000;  // rest between crank attempts
constexpr uint32_t STOP_HOLD_MS = 2000;     // hold START this long to switch off
constexpr uint32_t SHORT_PRESS_MAX_MS = 1000;
constexpr uint32_t STALL_CONFIRM_MS = 1500;  // engine-sense only
constexpr uint32_t MENU_TIMEOUT_MS = 20000;
constexpr uint32_t ENROLL_TIMEOUT_MS = 15000;
constexpr int MAX_CARDS = 8;
constexpr int MAX_BAD_TAPS = 5;
constexpr uint32_t LOCKOUT_MS = 30000;
constexpr int ENC_STEPS_PER_DETENT = 4;  // try 2 if one click moves two items
constexpr uint8_t TFT_TAB = INITR_BLACKTAB;  // try INITR_GREENTAB if the image is offset

// ---------------------------------------------------------------- hardware objects
Adafruit_ST7735 tft(PIN_TFT_CS, PIN_TFT_DC, -1);
MFRC522 rfid(PIN_RFID_SS, UINT8_MAX);  // UINT8_MAX = no reset pin, use soft reset
Preferences prefs;

// Kept across watchdog/panic resets (not across power loss), so a firmware crash
// while driving does not cut the ignition.
constexpr uint32_t RTC_MAGIC = 0x4B49474E;  // "KIGN"
RTC_NOINIT_ATTR uint32_t rtcMagic;
RTC_NOINIT_ATTR uint32_t rtcIgnOn;

// ---------------------------------------------------------------- relays
bool ignOn = false;
bool startOn = false;
esp_timer_handle_t crankGuard = nullptr;

// IGN is latched with gpio_hold so the pad keeps its level through a CPU reset.
void setIgn(bool on) {
  gpio_num_t g = (gpio_num_t)PIN_IGN;
  gpio_set_direction(g, GPIO_MODE_OUTPUT);
  gpio_set_level(g, on ? 1 : 0);  // applied to the pad when the hold is released
  gpio_hold_dis(g);
  if (on) gpio_hold_en(g);
  ignOn = on;
  rtcMagic = RTC_MAGIC;
  rtcIgnOn = on ? 1 : 0;
}

void IRAM_ATTR startOffNow() {
  gpio_set_level((gpio_num_t)PIN_START, 0);
}

void crankGuardFired(void*) {
  // Runs from the esp_timer task, independent of loop(): if loop() ever hangs
  // mid-crank, the starter is still released.
  startOffNow();
}

void setStart(bool on) {
  gpio_num_t g = (gpio_num_t)PIN_START;
  gpio_hold_dis(g);  // START is never held across resets
  gpio_set_direction(g, GPIO_MODE_OUTPUT);
  if (on) {
    esp_timer_stop(crankGuard);
    esp_timer_start_once(crankGuard, (uint64_t)(MAX_CRANK_MS + 250) * 1000ULL);
    gpio_set_level(g, 1);
  } else {
    gpio_set_level(g, 0);
    esp_timer_stop(crankGuard);
  }
  startOn = on;
}

// ---------------------------------------------------------------- inputs
DRAM_ATTR static const int8_t ENC_TABLE[16] = {0, -1, 1, 0, 1, 0, 0, -1,
                                               -1, 0, 0, 1, 0, 1, -1, 0};
volatile int32_t encRaw = 0;
volatile uint8_t encPrev = 0;

void IRAM_ATTR encoderISR() {
  // Read the register directly so this stays safe while flash is busy (NVS writes).
  uint32_t in = REG_READ(GPIO_IN_REG);
  uint8_t s = (((in >> PIN_ENC_A) & 1) << 1) | ((in >> PIN_ENC_B) & 1);
  encRaw += ENC_TABLE[(encPrev << 2) | s];
  encPrev = s;
}

int32_t encLastDetent = 0;
int readDetents() {
  noInterrupts();
  int32_t raw = encRaw;
  interrupts();
  int32_t detent = raw / ENC_STEPS_PER_DETENT;
  int diff = detent - encLastDetent;
  encLastDetent = detent;
  return diff;
}

bool interlockActive() {
  if (!REQUIRE_INTERLOCK) return true;
  return digitalRead(PIN_INTERLOCK) == LOW;
}

bool engineRunning() {
  return ENGINE_SENSE_ENABLED && digitalRead(PIN_ENGINE_SENSE) == LOW;
}

struct Button {
  bool down = false;
  bool rawLast = false;
  uint32_t rawChangedAt = 0;
  uint32_t downAt = 0;
  bool handled = false;  // a long-press action already used this press
  bool pressed = false;  // edge flags, valid for one loop()
  bool released = false;
  uint32_t releasedAfter = 0;

  void update(uint32_t now) {
    pressed = released = false;
    bool raw = digitalRead(PIN_BUTTON) == LOW;
    if (raw != rawLast) {
      rawLast = raw;
      rawChangedAt = now;
    }
    if (raw != down && now - rawChangedAt >= 30) {
      down = raw;
      if (down) {
        downAt = now;
        handled = false;
        pressed = true;
      } else {
        released = true;
        releasedAfter = now - downAt;
      }
    }
  }
  uint32_t heldFor(uint32_t now) const { return down ? now - downAt : 0; }
} btn;

// ---------------------------------------------------------------- card storage
struct Card {
  uint8_t len;
  uint8_t uid[10];
};
Card cards[MAX_CARDS];
uint8_t cardCount = 0;

void loadCards() {
  prefs.begin("keyless", true);
  cardCount = prefs.getUChar("n", 0);
  if (cardCount > MAX_CARDS) cardCount = 0;
  if (cardCount > 0 &&
      prefs.getBytes("cards", cards, sizeof(Card) * cardCount) != sizeof(Card) * cardCount) {
    cardCount = 0;
  }
  prefs.end();
}

void saveCards() {
  prefs.begin("keyless", false);
  if (cardCount > 0) prefs.putBytes("cards", cards, sizeof(Card) * cardCount);
  prefs.putUChar("n", cardCount);
  prefs.end();
}

int findCard(const Card& c) {
  for (int i = 0; i < cardCount; i++) {
    if (cards[i].len == c.len && memcmp(cards[i].uid, c.uid, c.len) == 0) return i;
  }
  return -1;
}

uint32_t lastRfidPoll = 0;
bool pollCard(uint32_t now, Card& out) {
  if (now - lastRfidPoll < 150) return false;
  lastRfidPoll = now;
  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) return false;
  out.len = rfid.uid.size > 10 ? 10 : rfid.uid.size;
  memcpy(out.uid, rfid.uid.uidByte, out.len);
  rfid.PICC_HaltA();
  return true;
}

// ---------------------------------------------------------------- state machine
enum class Mode { Setup, Locked, Armed, Priming, IgnOn, Cranking, Running, Menu, Enroll };
Mode mode = Mode::Locked;
uint32_t modeSince = 0;
uint32_t armedUntil = 0;
bool armedByMaster = false;
uint32_t crankStartedAt = 0;
uint32_t lastCrankEndedAt = 0;
bool hasCranked = false;
uint32_t stallSince = 0;
int badTaps = 0;
uint32_t lockoutUntil = 0;
int menuIndex = 0;
uint32_t menuActivityAt = 0;

String flashMsg;
uint32_t flashUntil = 0;
bool dirty = true;

const char* const MENU_ITEMS[] = {"Add card", "Remove others", "Back"};
constexpr int MENU_COUNT = 3;

void setMode(Mode m, uint32_t now) {
  mode = m;
  modeSince = now;
  dirty = true;
}

void flash(const String& msg, uint32_t now, uint32_t ms = 2000) {
  flashMsg = msg;
  flashUntil = now + ms;
  dirty = true;
}

void arm(bool master, uint32_t now) {
  armedByMaster = master;
  armedUntil = now + ARM_WINDOW_MS;
  badTaps = 0;
  setMode(Mode::Armed, now);
}

bool crankAllowed(uint32_t now) {
  if (!interlockActive()) return false;
  if (engineRunning()) return false;
  if (hasCranked && now - lastCrankEndedAt < CRANK_COOLDOWN_MS) return false;
  return true;
}

void beginCrank(uint32_t now) {
  setStart(true);
  crankStartedAt = now;
  btn.handled = true;
  setMode(Mode::Cranking, now);
}

void endCrank(uint32_t now) {
  setStart(false);
  hasCranked = true;
  lastCrankEndedAt = now;
  stallSince = 0;
  if (ENGINE_SENSE_ENABLED && !engineRunning()) {
    setMode(Mode::IgnOn, now);
    flash("Did not start", now);
  } else {
    setMode(Mode::Running, now);
  }
}

void switchOff(uint32_t now) {
  setStart(false);
  setIgn(false);
  btn.handled = true;
  arm(armedByMaster, now);  // allow a restart without re-tapping for a minute
  flash("Ignition OFF", now);
}

// ---------------------------------------------------------------- display
void drawCentered(const char* text, int y, uint8_t size, uint16_t color) {
  int16_t x1, y1;
  uint16_t w, h;
  tft.setTextSize(size);
  tft.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  tft.setCursor((tft.width() - w) / 2, y);
  tft.setTextColor(color);
  tft.print(text);
}

void draw(uint32_t now) {
  const char* title = "";
  const char* line1 = "";
  const char* line2 = "";
  uint16_t color = ST77XX_WHITE;
  char buf[32];

  switch (mode) {
    case Mode::Setup:
      title = "SETUP"; color = ST77XX_MAGENTA;
      line1 = "Tap your MASTER"; line2 = "card to enroll";
      break;
    case Mode::Locked:
      title = "LOCKED"; color = ST77XX_RED;
      if (now < lockoutUntil) {
        snprintf(buf, sizeof(buf), "Wait %lus", (unsigned long)((lockoutUntil - now) / 1000 + 1));
        line1 = "Too many bad taps"; line2 = buf;
      } else {
        line1 = "Tap card"; line2 = "to unlock";
      }
      break;
    case Mode::Armed:
      title = "READY"; color = ST77XX_YELLOW;
      snprintf(buf, sizeof(buf), "%lus left", (unsigned long)((armedUntil - now) / 1000 + 1));
      line1 = REQUIRE_INTERLOCK ? "Pedal+hold: START" : "Press: IGN ON";
      line2 = buf;
      break;
    case Mode::Priming:
      title = "PRIMING"; color = ST77XX_CYAN;
      line1 = "Keep holding..."; line2 = "";
      break;
    case Mode::IgnOn:
      title = "IGN ON"; color = ST77XX_CYAN;
      line1 = REQUIRE_INTERLOCK ? "Pedal + hold START" : "Hold START";
      line2 = "Hold 2s: OFF";
      break;
    case Mode::Cranking:
      title = "CRANKING"; color = ST77XX_ORANGE;
      line1 = "Release when"; line2 = "engine starts";
      break;
    case Mode::Running:
      title = "RUNNING"; color = ST77XX_GREEN;
      line1 = "Hold START 2s"; line2 = "to switch off";
      break;
    case Mode::Menu:
      title = "MENU"; color = ST77XX_WHITE;
      break;
    case Mode::Enroll:
      title = "ADD CARD"; color = ST77XX_MAGENTA;
      snprintf(buf, sizeof(buf), "%d/%d saved", cardCount, MAX_CARDS);
      line1 = "Tap the new card"; line2 = buf;
      break;
  }

  tft.fillScreen(ST77XX_BLACK);
  tft.fillRect(0, 0, tft.width(), 34, color);
  tft.setTextColor(ST77XX_BLACK);
  drawCentered(title, 9, 2, ST77XX_BLACK);

  if (mode == Mode::Menu) {
    for (int i = 0; i < MENU_COUNT; i++) {
      int y = 46 + i * 22;
      if (i == menuIndex) tft.fillRect(8, y - 4, tft.width() - 16, 18, ST77XX_BLUE);
      tft.setTextSize(1);
      tft.setTextColor(ST77XX_WHITE);
      tft.setCursor(16, y + 1);
      tft.print(MENU_ITEMS[i]);
    }
  } else {
    drawCentered(line1, 50, 1, ST77XX_WHITE);
    drawCentered(line2, 66, 1, ST77XX_WHITE);
  }

  // Footer: relay state and pedal input
  tft.setTextSize(1);
  tft.setCursor(4, tft.height() - 12);
  tft.setTextColor(ignOn ? ST77XX_GREEN : ST77XX_WHITE);
  tft.print(ignOn ? "IGN:ON " : "IGN:off");
  tft.setCursor(56, tft.height() - 12);
  tft.setTextColor(startOn ? ST77XX_ORANGE : ST77XX_WHITE);
  tft.print(startOn ? "ST:ON " : "ST:off");
  if (REQUIRE_INTERLOCK) {
    tft.setCursor(104, tft.height() - 12);
    tft.setTextColor(interlockActive() ? ST77XX_GREEN : ST77XX_WHITE);
    tft.print(interlockActive() ? "PEDAL" : "pedal");
  }

  if (now < flashUntil) {
    tft.fillRect(0, 84, tft.width(), 22, ST77XX_WHITE);
    drawCentered(flashMsg.c_str(), 91, 1, ST77XX_BLACK);
  }
}

// ---------------------------------------------------------------- setup
void setup() {
  // 1) Relays first, before anything else can delay us.
  esp_timer_create_args_t args = {};
  args.callback = &crankGuardFired;
  args.name = "crank_guard";
  esp_timer_create(&args, &crankGuard);

  esp_reset_reason_t why = esp_reset_reason();
  bool warmReset = why == ESP_RST_SW || why == ESP_RST_PANIC || why == ESP_RST_INT_WDT ||
                   why == ESP_RST_TASK_WDT || why == ESP_RST_WDT;
  bool restoreIgn = warmReset && rtcMagic == RTC_MAGIC && rtcIgnOn == 1;
  setStart(false);
  setIgn(restoreIgn);

  // 2) Inputs
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  pinMode(PIN_INTERLOCK, INPUT_PULLUP);
  pinMode(PIN_ENGINE_SENSE, INPUT_PULLUP);
  pinMode(PIN_ENC_A, INPUT_PULLUP);
  pinMode(PIN_ENC_B, INPUT_PULLUP);
  encPrev = (digitalRead(PIN_ENC_A) << 1) | digitalRead(PIN_ENC_B);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_A), encoderISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_B), encoderISR, CHANGE);

  Serial.begin(115200);

  // 3) SPI devices
  SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI, -1);
  pinMode(PIN_RFID_SS, OUTPUT);
  digitalWrite(PIN_RFID_SS, HIGH);
  tft.initR(TFT_TAB);
  tft.setRotation(1);  // landscape 160x128
  tft.fillScreen(ST77XX_BLACK);
  rfid.PCD_Init();

  loadCards();

  uint32_t now = millis();
  if (restoreIgn) {
    // Firmware restarted while the engine was on: carry on as if nothing happened.
    hasCranked = true;
    lastCrankEndedAt = now;
    setMode(Mode::Running, now);
    flash("Recovered after reset", now, 3000);
  } else if (cardCount == 0) {
    setMode(Mode::Setup, now);
  } else {
    setMode(Mode::Locked, now);
  }

  enableLoopWDT();
  Serial.printf("Keyless ignition ready. Reset reason %d, %d card(s), IGN restored: %d\n",
                (int)why, cardCount, (int)restoreIgn);
}

// ---------------------------------------------------------------- loop
uint32_t lastDraw = 0;
bool lastInterlock = false;

void loop() {
  feedLoopWDT();
  uint32_t now = millis();
  btn.update(now);
  int turns = readDetents();
  Card card;

  switch (mode) {
    case Mode::Setup:
      if (pollCard(now, card)) {
        cards[0] = card;
        cardCount = 1;
        saveCards();
        flash("Master card saved", now);
        arm(true, now);
      }
      break;

    case Mode::Locked:
      if (now < lockoutUntil) {
        if (now - lastDraw > 1000) dirty = true;
        break;
      }
      if (pollCard(now, card)) {
        int idx = findCard(card);
        if (idx >= 0) {
          arm(idx == 0, now);
          flash(idx == 0 ? "Master card" : "Card accepted", now, 1200);
        } else {
          badTaps++;
          flash("Unknown card", now);
          if (badTaps >= MAX_BAD_TAPS) {
            lockoutUntil = now + LOCKOUT_MS;
            badTaps = 0;
          }
        }
      }
      break;

    case Mode::Armed:
      if ((int32_t)(now - armedUntil) >= 0) {
        setMode(Mode::Locked, now);
        break;
      }
      if (now - lastDraw > 1000) dirty = true;  // countdown
      if (pollCard(now, card) && findCard(card) >= 0) {
        arm(findCard(card) == 0, now);
      }
      if (turns != 0 && armedByMaster && !btn.down) {
        menuIndex = 0;
        menuActivityAt = now;
        setMode(Mode::Menu, now);
        break;
      }
      if (btn.pressed && crankAllowed(now)) {
        // Pedal down + press: ignition on, prime, then crank while still held.
        setIgn(true);
        setMode(Mode::Priming, now);
      } else if (btn.released && !btn.handled && btn.releasedAfter < SHORT_PRESS_MAX_MS) {
        setIgn(true);
        hasCranked = false;
        setMode(Mode::IgnOn, now);
      }
      break;

    case Mode::Priming:
      if (!btn.down) {
        hasCranked = false;
        setMode(Mode::IgnOn, now);
      } else if (now - modeSince >= PRIME_MS) {
        if (crankAllowed(now)) beginCrank(now);
        else { btn.handled = true; setMode(Mode::IgnOn, now); }
      }
      break;

    case Mode::IgnOn:
      if (engineRunning()) {
        setMode(Mode::Running, now);
        break;
      }
      if (btn.pressed) {
        if (crankAllowed(now)) beginCrank(now);
        else if (interlockActive() && hasCranked) flash("Starter cooling down", now);
      } else if (btn.down && !btn.handled && btn.heldFor(now) >= STOP_HOLD_MS) {
        switchOff(now);
      }
      break;

    case Mode::Cranking:
      if (!btn.down || !interlockActive() || now - crankStartedAt >= MAX_CRANK_MS ||
          (ENGINE_SENSE_ENABLED && engineRunning() && now - crankStartedAt > 300)) {
        endCrank(now);
      }
      break;

    case Mode::Running:
      if (btn.down && !btn.handled && btn.heldFor(now) >= STOP_HOLD_MS) {
        switchOff(now);
        break;
      }
      if (ENGINE_SENSE_ENABLED) {
        if (!engineRunning()) {
          if (stallSince == 0) stallSince = now;
          if (now - stallSince >= STALL_CONFIRM_MS) {
            setMode(Mode::IgnOn, now);
            flash("Engine stopped", now);
          }
        } else {
          stallSince = 0;
        }
      }
      break;

    case Mode::Menu:
      if (turns != 0) {
        menuIndex = ((menuIndex + turns) % MENU_COUNT + MENU_COUNT) % MENU_COUNT;
        menuActivityAt = now;
        armedUntil = now + ARM_WINDOW_MS;
        dirty = true;
      }
      if (now - menuActivityAt > MENU_TIMEOUT_MS) {
        arm(armedByMaster, now);
        break;
      }
      if (btn.pressed) {
        btn.handled = true;
        menuActivityAt = now;
        if (menuIndex == 0) {
          if (cardCount >= MAX_CARDS) flash("Card list full", now);
          else setMode(Mode::Enroll, now);
        } else if (menuIndex == 1) {
          cardCount = cardCount > 0 ? 1 : 0;  // keep only the master
          saveCards();
          flash("Other cards removed", now);
        } else {
          arm(armedByMaster, now);
        }
      }
      break;

    case Mode::Enroll:
      if (now - modeSince > ENROLL_TIMEOUT_MS || btn.pressed) {
        btn.handled = true;
        menuActivityAt = now;
        setMode(Mode::Menu, now);
        break;
      }
      if (pollCard(now, card)) {
        if (findCard(card) >= 0) {
          flash("Already saved", now);
        } else {
          cards[cardCount++] = card;
          saveCards();
          flash("Card added", now);
        }
        menuActivityAt = now;
        setMode(Mode::Menu, now);
      }
      break;
  }

  // Safety net: the starter may only be energised in Cranking mode.
  if (startOn && mode != Mode::Cranking) setStart(false);

  bool il = interlockActive();
  if (il != lastInterlock) {
    lastInterlock = il;
    dirty = true;
  }
  if (flashUntil != 0 && now >= flashUntil) {
    flashUntil = 0;
    dirty = true;
  }
  if (dirty) {
    dirty = false;
    lastDraw = now;
    draw(now);
  }
}
