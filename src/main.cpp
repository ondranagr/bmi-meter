#include <Arduino.h>
#include "HX711.h"
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// --- Pin Definitions ---
const int PIN_SCALE_DOUT = 3;
const int PIN_SCALE_CLK = 2;
const int PIN_US_TRIG = 10;
const int PIN_US_ECHO = 9;

// --- Constants ---
const float SENSOR_MOUNT_HEIGHT_CM = 250.0; // Ultrasonic sensor height from floor
const float SCALE_CALIBRATION_FACTOR = -21300.0;
const int LCD_COLS = 16;
const int LCD_ROWS = 2;
const int LCD_I2C_ADDR = 0x27;
const int SCALE_SAMPLES = 5; // Average over 5 readings
const unsigned long LOOP_DELAY_MS = 500;
const float SOUND_TIME_US_PER_CM = 29.15452;
const unsigned long US_TIMEOUT_US = 30000; // 30ms timeout for ultrasonic pulse

// --- Stability Check Constants ---
const float WEIGHT_TOLERANCE_KG = 2.0; // Maximum weight difference for stability
const float HEIGHT_TOLERANCE_CM = 3.0; // Maximum height difference for stability
const int STABLE_READINGS_REQUIRED = 5; // Number of consecutive stable readings needed

// --- Scratch memory ---
char buffer[50];

// BMI display class
struct BMI_Display : LiquidCrystal_I2C {
  BMI_Display()
    : LiquidCrystal_I2C(LCD_I2C_ADDR, LCD_COLS, LCD_ROWS) {
    memcpy(row1, emptyline, LCD_COLS);
    memcpy(row2, emptyline, LCD_COLS);
    row1[LCD_COLS] = row2[LCD_COLS] = 0; // zerobyte at the end of the strings so lcd.print() works correctly
  }

  char row1[LCD_COLS+1], row2[LCD_COLS+1];
  const char* emptyline = "               ";
  char
    *lcd_weight = row1,
    *lcd_height = row2,
    *lcd_bmi_value = row1 + 12,
    *lcd_bmi_word = row2 + 9;

  const char* bmi_words[4] = {
    "podvaha",
    "v norme",
    "nadvaha",
    "obezita"
  };

  const float bmi_values[6][3] = {
    {13.0, 16.5, 18.0},
    {13.5, 18.0, 20.0},
    {14.0, 19.5, 22.5},
    {15.5, 22.5, 25.0},
    {17.0, 24.0, 28.0},
    {19.0, 25.0, 30.0}
  };

  int weight = 0, height = 0;

  void init() {
    LiquidCrystal_I2C::init();
    backlight();
    clear();
  }

  void update() {
    this->setCursor(0, 0);
    this->print(row1);
    this->setCursor(0, 1);
    this->print(row2);
  }

  void setWeight(int weight) {
    this->weight = weight;
    printValue(lcd_weight, 8, "%d kg", weight);
  }

  void setHeight(int height) {
    this->height = height;
    printValue(lcd_height, 7, "%d cm", height);
  }

  void updateBMI() {
    float bmi = 10000.0 * weight / height / height;
    memcpy(lcd_bmi_value - 4, "BMI=", 4);
    printValue(lcd_bmi_value, 4, bmi);
    int i = getHeightIndex();
    int index = 0;
    if(bmi > bmi_values[i][0]) index = 1;
    if(bmi > bmi_values[i][1]) index = 2;
    if(bmi > bmi_values[i][2]) index = 3;
    memcpy(lcd_bmi_word, bmi_words[index], 7);
  }

  void message(const char* line1, const char* line2 = "") {
    int len1 = strlen(line1), len2 = strlen(line2);
    if (len1 > LCD_COLS || len2 > LCD_COLS) return;
    // Clear entire display buffer first
    memset(row1, ' ', LCD_COLS);
    memset(row2, ' ', LCD_COLS);
    row1[LCD_COLS] = row2[LCD_COLS] = 0;
    // Center and copy the new text
    memcpy(row1 + (LCD_COLS-len1)/2, line1, len1);
    memcpy(row2 + (LCD_COLS-len2)/2, line2, len2);
  }

private:
  void printValue(char* position, int size, const char* format, int value) {
    int chars = snprintf(position, size, format, value);
    if (size > chars) memcpy(position + chars, emptyline, size - chars); // right pad spaces, avoid zerobytes
  }
  void printValue(char* position, int size, float value) {
    dtostrf(value, 2, 1, buffer); // Arduino does not support snprintf with %f
    memcpy(position, buffer, size);
    int chars = strlen(position);
    if (size > chars) memcpy(position + chars, emptyline, size - chars);
  }

  int getHeightIndex() {
    const int height_groups[5] = { 115, 130, 145, 155, 165 }; // cm
    for(int i=0; i<5; ++i) if (height < height_groups[i]) return i;
    return 5;
  }
};

// --- Stability Tracking ---
struct StabilityTracker {
  float lastWeight = 0;
  float lastHeight = 0;
  int stableCount = 0;
  bool wasStable = false;

  bool checkStability(float currentWeight, float currentHeight, bool &movementDetected) {
    movementDetected = false;
    
    bool stable = (fabs(currentWeight - lastWeight) <= WEIGHT_TOLERANCE_KG) &&
                  (fabs(currentHeight - lastHeight) <= HEIGHT_TOLERANCE_CM);
    
    if (stable) {
      stableCount++;
    } else {
      stableCount = 0;
      movementDetected = true;
    }

    lastWeight = currentWeight;
    lastHeight = currentHeight;

    return stableCount >= STABLE_READINGS_REQUIRED;
  }

  void reset() {
    stableCount = 0;
    lastWeight = 0;
    lastHeight = 0;
    wasStable = false;
  }
};

// --- Hardware Objects ---
HX711 scale;
BMI_Display lcd;
StabilityTracker stability;

// --- Function Prototypes ---
float measureHeightCm();
float measureWeightKg();

void setup() {
  Serial.begin(9600);

  pinMode(PIN_US_TRIG, OUTPUT);
  pinMode(PIN_US_ECHO, INPUT);

  lcd.init();

  scale.begin(PIN_SCALE_DOUT, PIN_SCALE_CLK);
  scale.set_scale(SCALE_CALIBRATION_FACTOR);
  delay(200); // Allow scale to stabilize
  scale.tare(); // Reset scale to 0
}

void loop() {
  float currentHeight = measureHeightCm();
  float currentWeight = measureWeightKg();

  Serial.print("Height: ");
  Serial.print(currentHeight);
  Serial.print(" cm, Weight: ");
  Serial.print(currentWeight);
  Serial.println(" kg");

  // Check if person is on the scale
  if (currentWeight < 10 || currentHeight < 100) {
    lcd.message("Stoupni si", "na vahu");
    lcd.update();
    stability.reset();
    delay(LOOP_DELAY_MS);
    return;
  }

  // Check if measurements are stable
  bool movementDetected = false;
  bool isStable = stability.checkStability(currentWeight, currentHeight, movementDetected);
  
  if (movementDetected) {
    // Movement detected - ask user to stay still
    lcd.message("Stuj klidne", "a rovne");
    lcd.update();
    delay(LOOP_DELAY_MS);
    return;
  }
  
  if (!isStable) {
    // Checking for value stabilization is in progress
    lcd.message("Probiha", "mereni...");
    lcd.update();
    delay(LOOP_DELAY_MS);
    return;
  }

  // Measurements are valid and stable - display results
  // Clear both rows first to avoid leftover characters from previous messages
  memcpy(lcd.row1, lcd.emptyline, LCD_COLS);
  memcpy(lcd.row2, lcd.emptyline, LCD_COLS);

  lcd.setWeight((int)currentWeight);
  lcd.setHeight((int)currentHeight);
  lcd.updateBMI();
  lcd.update();

  delay(LOOP_DELAY_MS);
}

float measureHeightCm() {
  // Trigger ultrasonic sensor
  digitalWrite(PIN_US_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(PIN_US_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_US_TRIG, LOW);

  long echoTime = pulseIn(PIN_US_ECHO, HIGH, US_TIMEOUT_US);

  if (echoTime == 0) {
    return -1; // No echo received (out of range)
  }

  float distanceCm = echoTime / (SOUND_TIME_US_PER_CM * 2);

  if (distanceCm > SENSOR_MOUNT_HEIGHT_CM || distanceCm < 10) {
    return -1; // Out of range
  }

  return SENSOR_MOUNT_HEIGHT_CM - distanceCm;
}

float measureWeightKg() {
  if (scale.is_ready()) {
    return scale.get_units(SCALE_SAMPLES); // Average weight over defined samples
  }

  return -1; // Scale not ready
}
