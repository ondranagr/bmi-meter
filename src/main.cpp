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
const unsigned long LOOP_DELAY_MS = 200;
const float US_ROUNDTRIP_CM_DIVISOR = 58.0; // Convert microseconds to cm
const unsigned long US_TIMEOUT_US = 30000; // 30ms timeout for ultrasonic pulse

// --- Hardware Objects ---
HX711 scale;
LiquidCrystal_I2C lcd(LCD_I2C_ADDR, LCD_COLS, LCD_ROWS);

// --- Function Prototypes ---
float measureHeightCm();
float measureWeightKg();
void updateDisplay(float height, float weight);

void setup() {
  Serial.begin(9600);

  pinMode(PIN_US_TRIG, OUTPUT);
  pinMode(PIN_US_ECHO, INPUT);

  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Initializing...");

  scale.begin(PIN_SCALE_DOUT, PIN_SCALE_CLK);
  scale.set_scale(SCALE_CALIBRATION_FACTOR);
  delay(200); // Allow scale to stabilize
  scale.tare(); // Reset scale to 0

  lcd.clear();
}

void loop() {
  float currentHeight = measureHeightCm();
  float currentWeight = measureWeightKg();

  // Clamp negative values to zero
  if (currentHeight < 0) currentHeight = 0;
  if (currentWeight < 0) currentWeight = 0;

  updateDisplay(currentHeight, currentWeight);

  Serial.print("Height: ");
  Serial.print(currentHeight);
  Serial.print(" Weight: ");
  Serial.println(currentWeight);

  delay(LOOP_DELAY_MS);
}

float measureHeightCm() {
  // Trigger ultrasonic sensor
  digitalWrite(PIN_US_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(PIN_US_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_US_TRIG, LOW);

  long duration = pulseIn(PIN_US_ECHO, HIGH, US_TIMEOUT_US);

  if (duration == 0) {
    return -1; // No echo received (out of range)
  }

  // Convert time to distance and subtract from sensor height
  float distanceCm = duration / US_ROUNDTRIP_CM_DIVISOR;

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

void updateDisplay(float height, float weight) {
  lcd.setCursor(0, 0);
  lcd.print("Weight: ");
  lcd.print(weight, 1);
  lcd.print(" kg   "); // Clear leftover digits

  lcd.setCursor(0, 1);
  lcd.print("Height: ");
  lcd.print((int)height);
  lcd.print(" cm   "); // Clear leftover digits
}
