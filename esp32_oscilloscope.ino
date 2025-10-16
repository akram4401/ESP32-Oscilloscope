#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

#define ANALOG_PIN 12
#define POT_PIN    15
#define BUTTON_PIN 5
#define SAMPLE_POINTS 128

enum Mode { MEASURE, CURSOR };
Mode currentMode = MEASURE;

int  samples[SAMPLE_POINTS];
int  sampleDelay = 50;         // µs
float sensitivity = 1.0;       // amplitude scale
int  offset = 0;               // vertical offset
int  cursorA = 30, cursorB = 90;
bool selectA = true;

bool buttonPressed = false;
unsigned long lastPressTime = 0;
int pressCount = 0;
const unsigned long pressGap = 400; // ms window for multiple presses

bool ampMode = false;  // false = time, true = amplitude

bool autoMode = false;
float autoFreq = 0;
int lastPotValue = 0;

void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT);
  pinMode(ANALOG_PIN, INPUT);
  pinMode(POT_PIN, INPUT);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 init failed!"));
    for (;;);
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("ESP32 MiniScope");
  display.display();
  delay(1000);
}

void loop() {
  handleButton();

  if (currentMode == MEASURE)
    runMeasure();
  else
    runCursor();
}

// ===================================================
//  MEASURE MODE
// ===================================================
void runMeasure() {
  int pot = analogRead(POT_PIN);

  // Disable auto mode if user moves potentiometer significantly
  if (autoMode && abs(pot - lastPotValue) > 50) {
    autoMode = false;
    flashText("AUTO OFF");
  }
  lastPotValue = pot;

  if (autoMode) {
    for (int i = 0; i < SAMPLE_POINTS; i++) {
      samples[i] = analogRead(ANALOG_PIN);
      delayMicroseconds(sampleDelay);
    }

    // Automatically calculate frequency and adjust view
    autoFreq = estimateFrequency(samples, SAMPLE_POINTS, sampleDelay);

    // Auto timebase adjust (try to keep ~2 cycles visible)
    if (autoFreq > 0) {
      float period_us = (1.0 / autoFreq) * 1e6;
      sampleDelay = constrain(period_us / (SAMPLE_POINTS / 2), 2, 1000);
    }

    drawWaveform(autoFreq, true);
    return;
  }

  // Normal manual control
  if (!ampMode)
    sampleDelay = map(pot, 0, 4095, 1, 1000);
  else
    sensitivity = map(pot, 0, 4095, 5, 50) / 10.0;

  for (int i = 0; i < SAMPLE_POINTS; i++) {
    samples[i] = analogRead(ANALOG_PIN);
    delayMicroseconds(sampleDelay);
  }

  float freq = estimateFrequency(samples, SAMPLE_POINTS, sampleDelay);
  drawWaveform(freq, true);
}

// ===================================================
//  CURSOR MODE
// ===================================================
void runCursor() {
  int pot = analogRead(POT_PIN);
  int pos = map(pot, 0, 4095, 0, SCREEN_WIDTH - 1);
  if (selectA) cursorA = pos; else cursorB = pos;

  drawWaveform(0, false);

  // Draw cursors
  display.drawLine(cursorA, 0, cursorA, SCREEN_HEIGHT - 1, SSD1306_WHITE);
  display.drawLine(cursorB, 0, cursorB, SCREEN_HEIGHT - 1, SSD1306_WHITE);

  // ΔT and frequency
  int deltaPix = abs(cursorB - cursorA);
  float deltaT = deltaPix * sampleDelay / 1e6;
  float freq = (deltaT > 0) ? 1.0 / deltaT : 0;

  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print(selectA ? "Cursor:A " : "Cursor:B ");
  display.print("dT:");
  display.print(deltaT * 1000, 2);
  display.print("ms");

  display.setCursor(0, 10);
  display.print("Freq:");
  if (freq > 0) display.print(freq, 1);
  else display.print("--");
  display.print("Hz");

  display.display();
}

// ===================================================
//  DRAW WAVEFORM
// ===================================================
void drawWaveform(float freq, bool showFreq) {
  display.clearDisplay();
  for (int i = 0; i < SAMPLE_POINTS - 1; i++) {
    int y1 = map((int)(samples[i] * sensitivity) + offset, 0, 4095, SCREEN_HEIGHT - 1, 0);
    int y2 = map((int)(samples[i + 1] * sensitivity) + offset, 0, 4095, SCREEN_HEIGHT - 1, 0);
    y1 = constrain(y1, 0, SCREEN_HEIGHT - 1);
    y2 = constrain(y2, 0, SCREEN_HEIGHT - 1);
    display.drawLine(i, y1, i + 1, y2, SSD1306_WHITE);
  }

  if (showFreq) {
    display.setTextSize(1);
    display.setCursor(0, 0);
    if (autoMode) display.print("AUTO ");
    else display.print(ampMode ? "AMPL " : "TIME ");
    display.print(sampleDelay);
    display.print("us");

    display.setCursor(0, 10);
    display.print("Freq:");
    if (freq > 0) display.print(freq, 1);
    else display.print("--");
    display.println("Hz");
    display.display();
  }
}

// ===================================================
//  BUTTON HANDLER
// ===================================================
void handleButton() {
  bool state = digitalRead(BUTTON_PIN);

  if (state && !buttonPressed) {
    buttonPressed = true;
    pressCount++;
    lastPressTime = millis();
  }
  if (!state && buttonPressed) buttonPressed = false;

  if ((millis() - lastPressTime > pressGap) && pressCount > 0) {
    if (pressCount == 1) handleSinglePress();
    else if (pressCount == 2) handleDoublePress();
    else if (pressCount >= 3) handleTriplePress();
    pressCount = 0;
  }
}

// ===================================================
//  PRESS FUNCTIONS
// ===================================================
void handleSinglePress() {
  if (currentMode == MEASURE) {
    ampMode = !ampMode;
    flashText(ampMode ? "AMPL" : "TIME");
  } else if (currentMode == CURSOR) {
    selectA = !selectA;
    flashText(selectA ? "A" : "B");
  }
}

void handleDoublePress() {
  if (currentMode == MEASURE) {
    autoMode = true;
    flashText("AUTO");
  }
}

void handleTriplePress() {
  currentMode = (currentMode == MEASURE) ? CURSOR : MEASURE;
  autoMode = false;
  flashText(currentMode == MEASURE ? "MEASURE" : "CURSOR");
}

// ===================================================
//  FLASH TEXT
// ===================================================
void flashText(const char *msg) {
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(25, 25);
  display.print(msg);
  display.display();
  delay(350);
}

// ===================================================
//  IMPROVED FREQUENCY DETECTION (first up to second up)
// ===================================================
float estimateFrequency(int *data, int len, int delay_us) {
  int mid = 2048;
  int last = data[0];
  int f1 = -1, f2 = -1;
  int hysteresis = 20;

  // find two clean rising edges
  for (int i = 1; i < len; i++) {
    if (last < mid && data[i] >= mid + hysteresis) {
      if (f1 == -1) f1 = i;
      else { f2 = i; break; }
    }
    last = data[i];
  }

  if (f1 != -1 && f2 != -1) {
    float period = (f2 - f1) * delay_us / 1e6;
    if (period > 0) return 1.0 / period;
  }
  return 0;
}
