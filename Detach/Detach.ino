#include <FastLED.h>
#include "DFRobotDFPlayerMini.h"
#include <EEPROM.h>

#define LED_PIN     6
#define NUM_LEDS    60
#define BRIGHTNESS  80
uint8_t globalBrightness = BRIGHTNESS;

bool dfPlayerAvailable = false; 

// Face buttons
#define BTN_Y  2
#define BTN_X  3
#define BTN_B  4
#define BTN_A  5


// D-pad
#define BTN_DPAD_UP    10
#define BTN_DPAD_DOWN  11
#define BTN_DPAD_LEFT  9
#define BTN_DPAD_RIGHT 8


// Joystick Buttons (L3/R3)
#define BTN_L3  40
#define BTN_R3  42

// --- Motor Setup ---
//#define MOTOR_PIN 22 // Connect the purple wire to Pin 22

// Joysticks
#define JOY_L_X  A15
#define JOY_L_Y  A14
#define JOY_R_X  A12
#define JOY_R_Y  A11
// The joysticks were interefering with each other so we spaced them apart

#define BTN_TRIGGER_L 13
#define BTN_TRIGGER_R 12

// Replace your speed values at the top with these:
float baseSpeed  = 1.0;
float slowSpeed  = 0.5;  // nearly frozen
float fastSpeed  = 3.0;   // nearly strobing

#define BTN_BRIGHT_UP   51
#define BTN_BRIGHT_DOWN 50

float animSpeed = 1.0;
unsigned long lastSpeedAdjust = 0;
unsigned long lastBrightnessAdjust = 0;

#define HALF (NUM_LEDS / 2)


CRGB leds[NUM_LEDS];
DFRobotDFPlayerMini myDFPlayer;


struct Button {
  uint8_t pin;
  bool lastState;    // raw reading last loop
  bool stableState;  // last confirmed stable state
  unsigned long lastDebounce;
};


Button buttons[] = {
  { BTN_Y,          HIGH, HIGH, 0 },
  { BTN_X,          HIGH, HIGH, 0 },
  { BTN_B,          HIGH, HIGH, 0 },
  { BTN_A,          HIGH, HIGH, 0 },
  { BTN_DPAD_UP,    HIGH, HIGH, 0 },
  { BTN_DPAD_DOWN,  HIGH, HIGH, 0 },
  { BTN_DPAD_LEFT,  HIGH, HIGH, 0 },
  { BTN_DPAD_RIGHT, HIGH, HIGH, 0 },
  { BTN_L3,         HIGH, HIGH, 0 },
  { BTN_R3,         HIGH, HIGH, 0 },
};


const int NUM_BUTTONS = 10;
const unsigned long DEBOUNCE_DELAY = 50;
const unsigned long ANIM_DURATION  = 3000;


enum Mode {
 IDLE,
 LEVELUP, POISON, EXPLODE, DAMAGE,           
 LOWHEALTH, POWERSURGE, MAGICCAST, VICTORY, TIMERUNNINGOUT,
 HEAL, SHIELD, SPEEDBOOST, STEALTH, RESPAWN, COMBOSTREAK, COINCOLLECT,
 CRITICALHIT, BOSSINCOMING, ICEFREEZE, CHECKPOINT
};


unsigned long lastSaveTime = 0;
unsigned long totalMinutesPlayed = 0;
const unsigned long SAVE_INTERVAL = 60000; // 60,000 ms = 1 minute

// --- WEANING THRESHOLDS (in minutes) ---
const int DISABLE_SOUND_TIME = 60;  // Turn off sound after 1 hour
const int DISABLE_ANIM_TIME  = 120; // Turn off animations after 2 hours
const int DISABLE_LEDS_TIME  = 180; // Turn off all LEDs after 3 hours


Mode currentMode = IDLE;
unsigned long animStart = 0;


// -- Joystick Logic --


// New Center Values for off center joystick
int centerLX = 500;  // your measured center
int centerLY = 500;  // adjust if needed
int centerRX = 500;  // your measured center
int centerRY = 500;  // adjust if needed
#define DEADZONE 80


uint8_t leftHue  = 0;
uint8_t rightHue = 128;
bool leftCurrentlyActive  = false;
bool rightCurrentlyActive = false;

//unsigned long millis() {
  //return millis() * animSpeed;
//}

uint8_t joystickToHue(int x, int y) {
 int nx = map(x, 0, 1023, -127, 127);
 int ny = map(y, 0, 1023, -127, 127);
 float angle = atan2(ny, nx);


 // Calculate base hue
  uint8_t hue = (uint8_t)((angle + PI) / (2 * PI) * 255);


 // Reduce number of unique hues (bigger zones)
 // uint8_t stepSize = 64;   // try 16, 32, or 64
 // hue = (hue / stepSize) * stepSize;

 return hue;
}


void updateJoysticks() {
  // Read each pin twice to clear "ghost" voltages from the previous pin
  analogRead(JOY_L_X); int lx = analogRead(JOY_L_X); 
  analogRead(JOY_L_Y); int ly = analogRead(JOY_L_Y);
  analogRead(JOY_R_X); int rx = analogRead(JOY_R_X);
  analogRead(JOY_R_Y); int ry = analogRead(JOY_R_Y);

  // Apply your calibration math
  int lnx = lx - centerLX;
  int lny = ly - centerLY;
  int rnx = rx - centerRX;
  int rny = ry - centerRY;

  leftCurrentlyActive  = (abs(lnx) > DEADZONE || abs(lny) > DEADZONE);
  rightCurrentlyActive = (abs(rnx) > DEADZONE || abs(rny) > DEADZONE);

  if (leftCurrentlyActive)  { leftHue  = joystickToHue(lx, ly); }
  if (rightCurrentlyActive) { rightHue = joystickToHue(rx, ry); }

  uint8_t activeVal = 255;
  uint8_t idleVal   = beatsin8(15, 10, 40); 

  for (int i = 0; i < NUM_LEDS; i++) {
    bool isLeft = (i < HALF);
    if ((isLeft && leftCurrentlyActive) || (!isLeft && rightCurrentlyActive)) {
      leds[i] = CHSV(isLeft ? leftHue : rightHue, 255, activeVal);
    } else {
      leds[i] = CRGB(0, 0, idleVal); // Resting Blue
    }
  }
}


// ── Animations ───────────────────────────────────────────


void animIdle() {
 // Fallback — only runs if joysticks aren't connected
 float breath = (exp(sin(millis() / 2000.0 * PI)) - 0.36787944) * 108.0;
 fill_solid(leds, NUM_LEDS, CRGB(0, 0, (uint8_t)breath));
}


void animLevelUp() {
 int pos = (int)(((millis() - animStart) * animSpeed) / 20) % (NUM_LEDS + 8);
 fill_solid(leds, NUM_LEDS, CRGB::Black);
 CRGB trailColors[] = {
   CRGB(255, 215, 0),
   CRGB(255, 180, 0),
   CRGB(255, 100, 0),
   CRGB(220,  50, 0),
   CRGB(200,   0, 200),
   CRGB(100,   0, 200),
   CRGB(0,   100, 255),
   CRGB(0,    50, 150),
 };
 for (int i = 0; i < 8; i++) {
   int idx = pos - i;
   if (idx >= 0 && idx < NUM_LEDS) leds[idx] = trailColors[i];
 }
}


void animHeal() {
 unsigned long elapsed = (millis() - animStart) * animSpeed;
 if (elapsed < 200) {
   fill_solid(leds, NUM_LEDS, CRGB::Black);
   leds[NUM_LEDS / 2]     = CRGB(180, 255, 180);
   leds[NUM_LEDS / 2 - 1] = CRGB(180, 255, 180);
 } else if (elapsed < 800) {
   int progress = map(elapsed, 200, 800, 0, NUM_LEDS / 2);
   fill_solid(leds, NUM_LEDS, CRGB::Black);
   for (int i = 0; i <= progress; i++) {
     uint8_t brightness = map(i, 0, NUM_LEDS / 2, 255, 60);
     CRGB color = CRGB(0, brightness, brightness / 4);
     int left  = NUM_LEDS / 2 - i;
     int right = NUM_LEDS / 2 + i;
     if (left  >= 0)       leds[left]  = color;
     if (right < NUM_LEDS) leds[right] = color;
   }
 } else if (elapsed < 1600) {
   float breath = sin((elapsed - 800) / 800.0 * PI);
   uint8_t val = (uint8_t)(breath * 180);
   fill_solid(leds, NUM_LEDS, CRGB(0, val, val / 5));
 } else {
   for (int i = 0; i < NUM_LEDS; i++) leds[i].nscale8(230);
 }
}


void animExplode() {
 unsigned long elapsed = (millis() - animStart) * animSpeed;
 if (elapsed < 300) {
   int progress = map(elapsed, 0, 300, 0, NUM_LEDS / 2);
   fill_solid(leds, NUM_LEDS, CRGB::Black);
   for (int i = 0; i <= progress; i++) {
     leds[NUM_LEDS / 2 - i] = CRGB::White;
     leds[NUM_LEDS / 2 + i] = CRGB::White;
   }
 } else if (elapsed < 1600) {
   for (int i = 0; i < NUM_LEDS; i++) leds[i].nscale8(180);
   for (int i = 0; i < 8; i++) {
     int pos = random8(NUM_LEDS);
     uint8_t colorPick = random8(3);
     if      (colorPick == 0) leds[pos] = CRGB(255, random8(20,  80),  0);
     else if (colorPick == 1) leds[pos] = CRGB(255, random8(100, 180), 0);
     else                     leds[pos] = CRGB(255, 255, random8(0, 60));
   }
 } else {
   for (int i = 0; i < NUM_LEDS; i++) {
     leds[i].nscale8(210);
     if (random8() > 230) leds[random8(NUM_LEDS)] = CRGB(random8(80, 140), random8(0, 20), 0);
   }
 }
}


void animDamage() {
 unsigned long elapsed = (millis() - animStart) * animSpeed;
 if (elapsed < 100) {
   fill_solid(leds, NUM_LEDS, CRGB::White);
 } else if (elapsed < 400) {
   int impactPos = NUM_LEDS / 2;
   int progress  = map(elapsed, 100, 400, 0, NUM_LEDS / 2);
   fill_solid(leds, NUM_LEDS, CRGB::Black);
   for (int i = 0; i <= progress; i++) {
     int left  = impactPos - i;
     int right = impactPos + i;
     if (left  >= 0)       leds[left]  = CRGB(200, 0, 0);
     if (right < NUM_LEDS) leds[right] = CRGB(200, 0, 0);
   }
 } else if (elapsed < 1200) {
   for (int i = 0; i < NUM_LEDS; i++) leds[i].nscale8(160);
   for (int i = 0; i < 4; i++) {
     int pos = random8(NUM_LEDS);
     uint8_t intensity = random8(120, 255);
     leds[pos] = CRGB(intensity, 0, random8(0, 20));
   }
 } else {
   for (int i = 0; i < NUM_LEDS; i++) leds[i].nscale8(254);
 }
}


void animShield() {
 unsigned long elapsed = (millis() - animStart) * animSpeed;
 if (elapsed < 400) {
   int progress = map(elapsed, 0, 400, 0, NUM_LEDS / 2);
   fill_solid(leds, NUM_LEDS, CRGB::Black);
   for (int i = 0; i < progress; i++) {
     uint8_t brightness = map(i, 0, NUM_LEDS / 2, 60, 255);
     CRGB color = CRGB(0, brightness / 4, brightness);
     leds[i]                = color;
     leds[NUM_LEDS - 1 - i] = color;
   }
 } else if (elapsed < 2000) {
   float breath = sin((elapsed - 400) / 600.0 * PI);
   uint8_t val = 80 + (uint8_t)(breath * 120);
   fill_solid(leds, NUM_LEDS, CRGB(0, val / 6, val));
 } else {
   for (int i = 0; i < NUM_LEDS; i++) leds[i].nscale8(240);
 }
}


void animPowerSurge() {
 unsigned long elapsed = (millis() - animStart) * animSpeed;
 if (elapsed < 300) {
   fill_solid(leds, NUM_LEDS, CRGB::Black);
   for (int i = 0; i < 6; i++) {
     int pos = random8(NUM_LEDS);
     leds[pos] = CRGB(random8(0, 80), random8(100, 200), 255);
   }
 } else if (elapsed < 600) {
   uint8_t strobe = (millis() % 60) < 30 ? 255 : 0;
   fill_solid(leds, NUM_LEDS, CRGB(strobe / 4, strobe / 2, strobe));
 } else if (elapsed < 1600) {
   for (int i = 0; i < NUM_LEDS; i++) leds[i].nscale8(150);
   for (int i = 0; i < 10; i++) {
     int pos = random8(NUM_LEDS);
     leds[pos] = random8(2) == 0
       ? CRGB(200, 220, 255)
       : CRGB(0, random8(100, 255), 255);
   }
 } else {
   for (int i = 0; i < NUM_LEDS; i++) leds[i].nscale8(235);
 }
}


void animMagicCast() {
 unsigned long elapsed = (millis() - animStart) * animSpeed;
 if (elapsed < 300) {
   fill_solid(leds, NUM_LEDS, CRGB::Black);
   int core = NUM_LEDS / 2;
   leds[core]     = CRGB(180, 0, 255);
   leds[core - 1] = CRGB(120, 0, 180);
   leds[core + 1] = CRGB(120, 0, 180);
   for (int i = 0; i < 3; i++) {
     leds[random8(NUM_LEDS)] = CRGB(random8(60, 140), 0, random8(150, 255));
   }
 } else if (elapsed < 900) {
   int progress = map(elapsed, 300, 900, 0, NUM_LEDS / 2);
   fill_solid(leds, NUM_LEDS, CRGB::Black);
   for (int i = 0; i <= progress; i++) {
     uint8_t brightness = map(i, 0, NUM_LEDS / 2, 255, 40);
     CRGB color = CRGB(brightness / 2, 0, brightness);
     int left  = NUM_LEDS / 2 - i;
     int right = NUM_LEDS / 2 + i;
     if (left  >= 0)       leds[left]  = color;
     if (right < NUM_LEDS) leds[right] = color;
   }
 } else if (elapsed < 1800) {
   for (int i = 0; i < NUM_LEDS; i++) leds[i].nscale8(200);
   for (int i = 0; i < 5; i++) {
     leds[random8(NUM_LEDS)] = CRGB(random8(80, 180), 0, random8(180, 255));
   }
 } else {
   for (int i = 0; i < NUM_LEDS; i++) leds[i].nscale8(238);
 }
}


void animVictory() {
 unsigned long elapsed = (millis() - animStart) * animSpeed;
 if (elapsed < 1200) {
   int pos = map(elapsed, 0, 1200, 0, NUM_LEDS);
   for (int i = 0; i < NUM_LEDS; i++) {
     leds[i] = (i <= pos) ? (CRGB)CHSV(map(i, 0, NUM_LEDS, 0, 255), 255, 220) : (CRGB)CRGB::Black;
   }
 } else if (elapsed < 2000) {
   uint8_t offset = map(elapsed, 1200, 2000, 0, 255);
   for (int i = 0; i < NUM_LEDS; i++) {
     leds[i] = CHSV(map(i, 0, NUM_LEDS, 0, 255) + offset, 255, 220);
   }
 } else {
   fill_solid(leds, NUM_LEDS, CRGB(220, 220, 220));
 }
}


// ── Bonus animations (swap into enum + switch to try) ────


void animSpeedBoost() {
 int pos = ((millis() - animStart) / 20) % (NUM_LEDS + 6);
 fill_solid(leds, NUM_LEDS, CRGB::Black);
 CRGB trail[] = { CRGB(255,255,255), CRGB(200,200,255), CRGB(100,100,255), CRGB(50,50,200), CRGB(20,20,150), CRGB(0,0,80) };
 for (int i = 0; i < 6; i++) {
   int idx = pos - i;
   if (idx >= 0 && idx < NUM_LEDS) leds[idx] = trail[i];
 }
}


void animStealth() {
 unsigned long elapsed = (millis() - animStart) * animSpeed;
 if (elapsed < 800) {
   for (int i = 0; i < NUM_LEDS; i++) leds[i].nscale8(210);
 } else {
   fill_solid(leds, NUM_LEDS, CRGB::Black);
   if (random8() > 240) leds[random8(NUM_LEDS)] = CRGB(0, random8(10, 30), 0);
 }
}


void animRespawn() {
 unsigned long elapsed = (millis() - animStart) * animSpeed;
 if (elapsed < 1500) {
   int progress = map(elapsed, 0, 1500, 0, NUM_LEDS);
   fill_solid(leds, NUM_LEDS, CRGB::Black);
   for (int i = 0; i < progress; i++) {
     uint8_t w = map(i, 0, NUM_LEDS, 100, 255);
     leds[i] = CRGB(w, w * 0.85, w * 0.6);
   }
 } else if (elapsed < 2200) {
   float breath = sin((elapsed - 1500) / 700.0 * PI);
   uint8_t val = 180 + (uint8_t)(breath * 60);
   fill_solid(leds, NUM_LEDS, CRGB(val, val * 0.85, val * 0.6));
 } else {
   for (int i = 0; i < NUM_LEDS; i++) leds[i].nscale8(240);
 }
}


void animLowHealth() {
 unsigned long elapsed = (millis() - animStart) * animSpeed;
 float speed = map(elapsed, 0, 2500, 1000, 300);
 uint8_t val = (uint8_t)(abs(sin(elapsed / speed * PI)) * 220);
 fill_solid(leds, NUM_LEDS, CRGB(val, 0, 0));
}


void animPoison() {
 unsigned long elapsed = (millis() - animStart) * animSpeed;
 if (elapsed < 1000) {
   int progress = map(elapsed, 0, 1000, 0, NUM_LEDS / 2);
   fill_solid(leds, NUM_LEDS, CRGB::Black);
   for (int i = 0; i < progress; i++) {
     uint8_t b = map(i, 0, NUM_LEDS / 2, 255, 40);
     leds[i]                = CRGB(b / 4, b, 0);
     leds[NUM_LEDS - 1 - i] = CRGB(b / 4, b, 0);
   }
 } else {
   for (int i = 0; i < NUM_LEDS; i++) leds[i].nscale8(210);
   for (int i = 0; i < 4; i++) leds[random8(NUM_LEDS)] = CRGB(0, random8(80, 180), 0);
 }
}


void animIceFreeze() {
 unsigned long elapsed = (millis() - animStart) * animSpeed;
 if (elapsed < 1200) {
   int progress = map(elapsed, 0, 1200, 0, NUM_LEDS / 2);
   for (int i = 0; i < progress; i++) {
     uint8_t f = random8(180, 255);
     leds[i]                = CRGB(f * 0.8, f * 0.9, f);
     leds[NUM_LEDS - 1 - i] = CRGB(f * 0.8, f * 0.9, f);
   }
 } else if (elapsed < 2200) {
   for (int i = 0; i < NUM_LEDS; i++) {
     uint8_t s = random8(180, 255);
     leds[i] = CRGB(s * 0.8, s * 0.9, s);
   }
 } else {
   for (int i = 0; i < NUM_LEDS; i++) leds[i].nscale8(240);
 }
}


void animComboStreak() {
 unsigned long elapsed = (millis() - animStart) * animSpeed;
 int interval = map(elapsed, 0, 2500, 300, 60);
 if ((elapsed % interval) < interval / 2) {
   fill_solid(leds, NUM_LEDS, CHSV(map(elapsed, 0, 2500, 0, 255), 255, 220));
 } else {
   fill_solid(leds, NUM_LEDS, CRGB::Black);
 }
}


void animCoinCollect() {
 unsigned long elapsed = (millis() - animStart) * animSpeed;
 if (elapsed < 800) {
   fill_solid(leds, NUM_LEDS, CRGB::Black);
   for (int i = 0; i < 12; i++) {
     uint8_t b = random8(150, 255);
     leds[random8(NUM_LEDS)] = CRGB(b, b * 0.85, 0);
   }
 } else {
   for (int i = 0; i < NUM_LEDS; i++) leds[i].nscale8(220);
 }
}


void animCriticalHit() {
 unsigned long elapsed = (millis() - animStart) * animSpeed;
 if      (elapsed < 80)   fill_solid(leds, NUM_LEDS, CRGB::White);
 else if (elapsed < 200)  fill_solid(leds, NUM_LEDS, CRGB::Black);
 else if (elapsed < 400)  fill_solid(leds, NUM_LEDS, CRGB::White);
 else if (elapsed < 1800) fill_solid(leds, NUM_LEDS, CRGB(map(elapsed, 400, 1800, 200, 20), 0, 0));
 else { for (int i = 0; i < NUM_LEDS; i++) leds[i].nscale8(240); }
}


void animBossIncoming() {
 unsigned long elapsed = (millis() - animStart) * animSpeed;
 uint8_t intensity = map(elapsed, 0, 2500, 30, 255);
 uint8_t val = (uint8_t)(abs(sin(elapsed / 500.0 * PI)) * intensity);
 fill_solid(leds, NUM_LEDS, CRGB(val * 0.6, 0, 0));
 leds[0] = leds[NUM_LEDS - 1] = CRGB(val, 0, 0);
}


void animTimeRunningOut() {
 unsigned long elapsed = (millis() - animStart) * animSpeed;
 int interval = map(elapsed, 0, 2500, 400, 60);
 if ((elapsed % interval) < interval / 2) {
   uint8_t f = random8(200, 255);
   fill_solid(leds, NUM_LEDS, CRGB(f, f * 0.4, 0));
 } else {
   fill_solid(leds, NUM_LEDS, CRGB::Black);
 }
}


void animCheckpoint() {
 unsigned long elapsed = (millis() - animStart) * animSpeed;
 if      (elapsed < 150)  fill_solid(leds, NUM_LEDS, CRGB(100, 255, 100));
 else if (elapsed < 300)  fill_solid(leds, NUM_LEDS, CRGB::Black);
 else if (elapsed < 450)  fill_solid(leds, NUM_LEDS, CRGB(100, 255, 100));
 else if (elapsed < 600)  fill_solid(leds, NUM_LEDS, CRGB::Black);
 else if (elapsed < 1800) fill_solid(leds, NUM_LEDS, CRGB(0, map(elapsed, 600, 1800, 0, 160), 0));
 else { for (int i = 0; i < NUM_LEDS; i++) leds[i].nscale8(242); }
}


// ── Main ─────────────────────────────────────────────────


void checkButtons() {
  for (int i = 0; i < NUM_BUTTONS; i++) {
    bool reading = digitalRead(buttons[i].pin);

    if (reading != buttons[i].lastState) {
      buttons[i].lastDebounce = millis();
    }

    if ((millis() - buttons[i].lastDebounce) > DEBOUNCE_DELAY) {
      if (reading != buttons[i].stableState) {
        buttons[i].stableState = reading;

        if (reading == LOW) {
          
          // Stage 2 — no animations after 2 hours
          if (totalMinutesPlayed < DISABLE_ANIM_TIME) {
            currentMode = (Mode)(i + 1);
            animStart = millis();
          }

          // Stage 1 — no sound after 1 hour
          if (dfPlayerAvailable && totalMinutesPlayed < DISABLE_SOUND_TIME) {
            myDFPlayer.playMp3Folder(i + 1);
            delay(15);
          }
        }
      }
    }

    buttons[i].lastState = reading;
  }
}


void setup() {
  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(globalBrightness);

  // Show something immediately so you know the Arduino is alive
  fill_solid(leds, NUM_LEDS, CRGB::Blue);
  FastLED.show();

  //pinMode(MOTOR_PIN, OUTPUT);
  //digitalWrite(MOTOR_PIN, LOW); // Ensure motor is off at startup

  for (int i = 0; i < NUM_BUTTONS; i++) pinMode(buttons[i].pin, INPUT_PULLUP);
  pinMode(BTN_BRIGHT_UP, INPUT_PULLUP);
  pinMode(BTN_BRIGHT_DOWN, INPUT_PULLUP);
  pinMode(BTN_L3, INPUT_PULLUP);
  pinMode(BTN_R3, INPUT_PULLUP);
  pinMode(BTN_TRIGGER_L, INPUT_PULLUP);
  pinMode(BTN_TRIGGER_R, INPUT_PULLUP);

  Serial1.begin(9600);
  delay(2000);

  if (!myDFPlayer.begin(Serial1, false, true)) {
    // Flash red twice, then CONTINUE — don't freeze
    for (int i = 0; i < 2; i++) {
        fill_solid(leds, NUM_LEDS, CRGB::Red);
        FastLED.show(); delay(300);
        fill_solid(leds, NUM_LEDS, CRGB::Black);
        FastLED.show(); delay(300);
    }
    dfPlayerAvailable = false;
  } else {
    dfPlayerAvailable = true;
    myDFPlayer.volume(30);
    fill_solid(leds, NUM_LEDS, CRGB::Green);
    FastLED.show(); delay(500);
  }

  // DFPlayer OK — flash green briefly
  fill_solid(leds, NUM_LEDS, CRGB::Green);
  FastLED.show();
  delay(500);

  myDFPlayer.volume(20);

  EEPROM.get(0, totalMinutesPlayed);
  
  // If the Arduino is brand new, EEPROM reads as totally blank (max value). We reset it to 0.
  if (totalMinutesPlayed == 0xFFFFFFFF) {
    totalMinutesPlayed = 0;
    EEPROM.put(0, totalMinutesPlayed);
  }
}

void loop() {
  unsigned long now = millis(); // (Fixed the double declaration here)

  // 1. The Time Tracker
  if (now - lastSaveTime >= SAVE_INTERVAL) {
    totalMinutesPlayed++;           // Add one minute
    EEPROM.put(0, totalMinutesPlayed); // Save it to permanent memory
    lastSaveTime = now;
  }

  checkButtons();

  if (totalMinutesPlayed >= DISABLE_LEDS_TIME) {
    FastLED.clear();
    FastLED.show();
    //digitalWrite(MOTOR_PIN, LOW); // Make sure motor turns off too
    return; // Stops the rest of the loop from running!
  }

  // ── Brightness control (NON-BLOCKING) ──
  if (digitalRead(BTN_BRIGHT_UP) == LOW && now - lastBrightnessAdjust > 50) {
    globalBrightness = constrain(globalBrightness + 5, 10, 255);
    FastLED.setBrightness(globalBrightness);
    lastBrightnessAdjust = now;
  }

  if (digitalRead(BTN_BRIGHT_DOWN) == LOW && now - lastBrightnessAdjust > 50) {
    globalBrightness = constrain(globalBrightness - 5, 10, 255);
    FastLED.setBrightness(globalBrightness);
    lastBrightnessAdjust = now;
  }

  // ── Speed HOLD control (trigger-based) ──
  bool ltHeld = (digitalRead(BTN_TRIGGER_L) == LOW);
  bool rtHeld = (digitalRead(BTN_TRIGGER_R) == LOW);

  if      (ltHeld) animSpeed = slowSpeed;  // nearly frozen
  else if (rtHeld) animSpeed = fastSpeed;  // frenzy
  else             animSpeed = baseSpeed;  // normal

  // // ── Motor Control (Spins while ANY of the 10 buttons are held) ──
  // bool isAnyButtonHeld = false;
  // for (int i = 0; i < NUM_BUTTONS; i++) {
  //   if (digitalRead(buttons[i].pin) == LOW) {
  //     isAnyButtonHeld = true;
  //     break; // As soon as we find one held, stop checking
  //   }
  // }
  
  // if (isAnyButtonHeld) {
  //   digitalWrite(MOTOR_PIN, HIGH); // Spin motor
  // } else {
  //   digitalWrite(MOTOR_PIN, LOW);  // Stop motor
  // }

  // ── Reset animation ──
  if (currentMode != IDLE && (now - animStart) > ANIM_DURATION) {
    currentMode = IDLE;
  }

  switch (currentMode) {
    case IDLE:           updateJoysticks(); break;
    // --- Active (8 Face/D-pad + 2 Joystick Buttons = 10 Active) ---
    case LEVELUP:        animLevelUp();     break;
    case POISON:         animPoison();      break;
    case EXPLODE:        animExplode();     break;
    case DAMAGE:         animDamage();      break;
    case LOWHEALTH:      animLowHealth();   break;
    case POWERSURGE:     animPowerSurge();  break;
    case MAGICCAST:      animMagicCast();   break;
    case VICTORY:        animVictory();     break;
    case TIMERUNNINGOUT: animTimeRunningOut(); break;
    case HEAL:           animHeal();        break; // Triggered by L3
    case SHIELD:         animShield();      break; // Triggered by R3
  }

  FastLED.show();
}

