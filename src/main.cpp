#include <Arduino.h>
#include <FastLED.h>
#include <BleCombo.h>   // BlynkGO/ESP32-BLE-Combo: global Keyboard / Mouse

// ====== M5Atom Matrix (typical) ======
static constexpr int PIN_MATRIX_LED = 27;   // 5x5 WS2812
static constexpr int PIN_BTN        = 39;   // Button (G39)
static constexpr int PIN_DISABLE    = 33;   // G33 -> GND disables auto mouse

// ====== LEDs ======
static constexpr int NUM_LEDS = 25;
static constexpr EOrder LED_ORDER = GRB;
CRGB leds[NUM_LEDS];

// ====== User config ======
static const char* TEXT_TO_TYPE = "Hello from ATOM!\n";  // ←送信したい文字列に変更


static constexpr uint32_t DEBOUNCE_MS      = 40;
static constexpr uint32_t LED_EVENT_MS     = 1000;

static constexpr uint32_t MOUSE_PERIOD_MS  = 120000; // 2 min
static constexpr int8_t   MOUSE_STEP       = 10;     // 10 counts

// ====== Button debounce state ======
// GPIO39 has no internal pullup. Assume board provides external pull-up.
// released=HIGH, pressed=LOW
bool     lastRaw        = true;   // HIGH
bool     stable         = true;
uint32_t lastChangeMs   = 0;
bool     pressedEdge    = false;


unsigned long millis_buf=0;

// ====== Mouse schedule / sequence ======
uint32_t nextMouseMs = 0;

enum class JiggleState : uint8_t { Idle, Up, Down, Left, Right };
JiggleState jiggle = JiggleState::Idle;
uint32_t jiggleMs = 0;

// ====== LED override ======
bool     ledOverride = false;
CRGB     ledColor    = CRGB::Black;
uint32_t ledUntil    = 0;

// ====== Helpers ======
static inline bool mouseDisabled()
{
  // INPUT_PULLUP -> default HIGH, connect to GND -> LOW => disabled
  return digitalRead(PIN_DISABLE) == LOW;
}

static void setLedEvent(const CRGB& c, uint32_t ms)
{
  ledOverride = true;
  ledColor = c;
  ledUntil = millis() + ms;
}

static void showSolid(const CRGB& c)
{
  for (int i = 0; i < NUM_LEDS; i++) leds[i] = c;
  FastLED.show();
}

// Idle: time-varying gradient
static void renderIdleGradient()
{
  uint8_t t = (uint8_t)(millis() >> 4);
  for (int i = 0; i < NUM_LEDS; i++) {
    uint8_t hue = t + (uint8_t)(i * 7);
    leds[i] = CHSV(hue, 255, 60);  // modest brightness
  }
  FastLED.show();
}

static void updateLed()
{
  uint32_t now = millis();
  if (ledOverride) {
    if ((int32_t)(now - ledUntil) < 0) {
      showSolid(ledColor);
      return;
    }
    ledOverride = false;
  }
  renderIdleGradient();
}

static void updateButton()
{
  bool raw = (digitalRead(PIN_BTN) == HIGH); // HIGH=not pressed, LOW=pressed

  uint32_t now = millis();
  if (raw != lastRaw) {
    lastRaw = raw;
    lastChangeMs = now;
  }

  if (now - lastChangeMs >= DEBOUNCE_MS) {
    if (stable != lastRaw) {
      bool prev = stable;
      stable = lastRaw;

      // released(HIGH=true) -> pressed(LOW=false)
      if (prev == true && stable == false) {
        pressedEdge = true;
      }
    }
  }
}

// Keyboard action
static void doKeyboardAction()
{
  // このライブラリの接続判定は Keyboard.isConnected() を使うのが基本 :contentReference[oaicite:1]{index=1}
  if (!Keyboard.isConnected()) return;

  setLedEvent(CRGB::Red, LED_EVENT_MS);
  Keyboard.print(TEXT_TO_TYPE);
}

// Start mouse jiggle sequence (up/down/left/right)
static void startMouseJiggle()
{
  // Mouse側に isConnected() は無いので、Keyboard接続をゲートにする :contentReference[oaicite:2]{index=2}
  if (!Keyboard.isConnected()) return;
  if (mouseDisabled()) return;

  setLedEvent(CRGB::Blue, LED_EVENT_MS);

  jiggle = JiggleState::Up;
  jiggleMs = millis();
}

static void updateMouseJiggle()
{
  if (jiggle == JiggleState::Idle) return;

  if (!Keyboard.isConnected()) { jiggle = JiggleState::Idle; return; }
  if (mouseDisabled())         { jiggle = JiggleState::Idle; return; }

  uint32_t now = millis();
  if (now - jiggleMs < 25) return;
  jiggleMs = now;

  switch (jiggle) {
    case JiggleState::Up:
      Mouse.move(0, -MOUSE_STEP);
      jiggle = JiggleState::Down;
      break;
    case JiggleState::Down:
      Mouse.move(0, +MOUSE_STEP);
      jiggle = JiggleState::Left;
      break;
    case JiggleState::Left:
      Mouse.move(-MOUSE_STEP, 0);
      jiggle = JiggleState::Right;
      break;
    case JiggleState::Right:
      Mouse.move(+MOUSE_STEP, 0);
      jiggle = JiggleState::Idle;
      break;
    default:
      jiggle = JiggleState::Idle;
      break;
  }
}

void setup()
{
  Serial.begin(115200);

  pinMode(PIN_DISABLE, INPUT_PULLUP);
  pinMode(PIN_BTN, INPUT);  // GPIO39 input-only

  FastLED.addLeds<WS2812B, PIN_MATRIX_LED, LED_ORDER>(leds, NUM_LEDS);
  FastLED.setBrightness(64);
  showSolid(CRGB::Black);

  Keyboard.begin();
  Mouse.begin();

  nextMouseMs = millis() + MOUSE_PERIOD_MS;
}

void loop()
{
  
  updateButton();

  if (pressedEdge) {    
    pressedEdge = false;
    if (( millis() - millis_buf) > 1000) {
      doKeyboardAction();
      millis_buf = millis();
    }
  }

  uint32_t now = millis();
  if ((int32_t)(now - nextMouseMs) >= 0) {
    nextMouseMs = now + MOUSE_PERIOD_MS;
    startMouseJiggle();
  }

  updateMouseJiggle();
  updateLed();

  delay(5);
}

