#include <LiquidCrystal_I2C.h>
#include <SPI.h>
#include <MFRC522.h>
#include <IRremote.h>
#include <Servo.h>

LiquidCrystal_I2C lcd(0x27, 16, 2);
const uint16_t RECV_PIN = 2;
IRrecv irrecv(RECV_PIN);

// 7-segment display (via shift register)
const int dataPin = 8;
const int latchPin = 7;
const int clockPin = 6;

// Segment patterns for digits 0–9 (common-cathode)
byte newDigit[10] = {
  0x7E, 0x0C, 0xB6, 0x9E, 0xCC,
  0xDA, 0xFA, 0x0E, 0xFE, 0xDE
};

const int buzzerPin = 3;
const int digitBeepFreq = 400;
const int highGrantedFreq = 3000;
const int lowDeniedFreq = 150;
const int digitBeepDuration = 100;
const int accessGrantedBeepDuration = 750;
const int gapBetweenGrantedBeeps = 0;
const int longBuzzDuration = 2250;

String passcodeInput = "";
const String correctPasscode = "4747";
int attemptCount = 0;
bool lockedOut = false;

bool rfidStage = false;
int rfidAttemptCount = 0;

const int redLedPin = A1;
const int greenLedPin = A3;

// RFID setup
#define SS_PIN 10
#define RST_PIN 9
MFRC522 mfrc522(SS_PIN, RST_PIN);
byte allowedUID[4] = { 0x00, 0x64, 0x63, 0x82 };

// Ultrasonic sensor pins
const int trigPin = 5;
const int echoPin = A0;

// Photoresistor and light threshold
const int lightSensorPin = A2;
int lightThreshold = 100;

// Servo for door lock
Servo doorServo;

// Power management flags
bool powerSaving = true;
bool sleepMode = false;

unsigned long lastKeyPressTime = 0;
const unsigned long pinTimeout = 10000;

// Measures distance using ultrasonic sensor
long getDistance() {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  long duration = pulseIn(echoPin, HIGH, 30000);
  return duration * 0.0343 / 2;
}

// Beeps at specified frequency and duration
void beep(int frequency, int durationMs) {
  unsigned long period = 1000000UL / frequency;
  unsigned long onTime = period / 7;
  unsigned long offTime = period - onTime;
  unsigned long cycles = (unsigned long)frequency * durationMs / 1000UL;
  for (unsigned long i = 0; i < cycles; i++) {
    digitalWrite(buzzerPin, HIGH);
    delayMicroseconds(onTime);
    digitalWrite(buzzerPin, LOW);
    delayMicroseconds(offTime);
  }
}

// Smooth servo movement with sound
void servoMoveWithSound(int startAngle, int endAngle, int freq, int stepDelayMs) {
  if (startAngle < endAngle) {
    for (int angle = startAngle; angle <= endAngle; angle++) {
      doorServo.write(angle);
      beep(freq, 10);
      delay(stepDelayMs);
    }
  } else {
    for (int angle = startAngle; angle >= endAngle; angle--) {
      doorServo.write(angle);
      beep(freq, 10);
      delay(stepDelayMs);
    }
  }
}

// Unlocks door with sound and display prompts
void unlockDoor() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Unlocking...");
  servoMoveWithSound(0, 90, 500, 30);

  delay(500);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Enter Now,");
  lcd.setCursor(0, 1);
  lcd.print("Have a great day!");
  delay(5000);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Locking...");
  servoMoveWithSound(90, 0, 500, 30);

  for (int i = 0; i < 3; i++) {
    digitalWrite(redLedPin, HIGH);
    digitalWrite(greenLedPin, HIGH);
    delay(500);
    digitalWrite(redLedPin, LOW);
    digitalWrite(greenLedPin, LOW);
    delay(500);
  }

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("ON STANDBY");

  rfidStage = false;
  passcodeInput = "";
  attemptCount = 0;
  rfidAttemptCount = 0;
  powerSaving = true;
}

// Displays digit on 7-segment display
void displayDigit(int digit) {
  if (digit < 0 || digit > 9) return;
  digitalWrite(latchPin, LOW);
  shiftOut(dataPin, clockPin, MSBFIRST, newDigit[digit]);
  digitalWrite(latchPin, HIGH);
}

// Checks if UID matches allowed RFID tag
bool checkUidMatch(byte *uid, byte uidSize) {
  if (uidSize != 4) return false;
  for (byte i = 0; i < 4; i++) {
    if (uid[i] != allowedUID[i]) return false;
  }
  return true;
}

// Arduino setup
void setup() {
  Serial.begin(9600);
  irrecv.enableIRIn();

  pinMode(dataPin, OUTPUT);
  pinMode(latchPin, OUTPUT);
  pinMode(clockPin, OUTPUT);
  pinMode(buzzerPin, OUTPUT);
  pinMode(redLedPin, OUTPUT);
  pinMode(greenLedPin, OUTPUT);
  digitalWrite(buzzerPin, LOW);
  digitalWrite(redLedPin, LOW);
  digitalWrite(greenLedPin, LOW);

  digitalWrite(latchPin, LOW);
  shiftOut(dataPin, clockPin, MSBFIRST, 0x00);
  digitalWrite(latchPin, HIGH);

  SPI.begin();
  mfrc522.PCD_Init();

  lcd.init();
  lcd.backlight();
  delay(50);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("ON STANDBY");

  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  pinMode(lightSensorPin, INPUT);

  doorServo.attach(4);
  doorServo.write(0);

  lastKeyPressTime = millis();
}

// Arduino main loop
void loop() {
  int lightVal = analogRead(lightSensorPin);
  Serial.print("Light sensor reading: ");
  Serial.println(lightVal);

  if (lightVal < lightThreshold) {
    if (!sleepMode) {
      sleepMode = true;
      rfidStage = false;
      passcodeInput = "";
      attemptCount = 0;
      rfidAttemptCount = 0;
      powerSaving = true;
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("SLEEP MODE");
    }
    delay(500);
    return;
  } else if (sleepMode) {
    sleepMode = false;
    rfidStage = false;
    passcodeInput = "";
    attemptCount = 0;
    rfidAttemptCount = 0;
    powerSaving = true;
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("ON STANDBY");
    delay(1000);
  }

  if (!powerSaving && !rfidStage && (millis() - lastKeyPressTime > pinTimeout)) {
    passcodeInput = "";
    powerSaving = true;
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("ON STANDBY");
    return;
  }

  if (powerSaving) {
    long dist = getDistance();
    if (dist > 0 && dist < 61) {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("SCANNING USER");
      unsigned long scanStart = millis();
      while (millis() - scanStart < 4000) {
        beep(1500, 50);
        delay(450);
      }
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Welcome User,");
      lcd.setCursor(0, 1);
      lcd.print("Enter PIN Code");
      powerSaving = false;
      lastKeyPressTime = millis();
    } else {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("ON STANDBY");
      delay(500);
      return;
    }
  }

  if (lockedOut) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("FULL SYSTEM");
    lcd.setCursor(0, 1);
    lcd.print("LOCKDOWN!");
    unsigned long startTime = millis();
    while (millis() - startTime < 6000) {
      digitalWrite(redLedPin, HIGH);
      beep(100, 500);
      digitalWrite(redLedPin, LOW);
      delay(500);
    }
    for (int i = 7; i >= 1; i--) {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Lock resets in:");
      lcd.setCursor(0, 1);
      lcd.print(i);
      delay(1000);
    }
    lockedOut = false;
    powerSaving = true;
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("ON STANDBY");
    return;
  }

  if (rfidStage) {
    if (!mfrc522.PICC_IsNewCardPresent() || !mfrc522.PICC_ReadCardSerial()) return;

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("PROCESSING...");
    lcd.setCursor(0, 1);
    lcd.print("PLEASE WAIT");
    delay(1000);

    if (checkUidMatch(mfrc522.uid.uidByte, mfrc522.uid.size)) {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("RFID TAG OK!");
      lcd.setCursor(0, 1);
      lcd.print("WELCOME USER");
      digitalWrite(greenLedPin, HIGH);
      beep(highGrantedFreq, accessGrantedBeepDuration);
      delay(2000);
      digitalWrite(greenLedPin, LOW);
      unlockDoor();
    } else {
      rfidAttemptCount++;
      if (rfidAttemptCount >= 3) {
        lockedOut = true;
      } else {
        int attemptsLeft = 3 - rfidAttemptCount;
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("INCORRECT RFID");
        lcd.setCursor(0, 1);
        lcd.print(attemptsLeft);
        lcd.print(" left");
        digitalWrite(redLedPin, HIGH);
        beep(lowDeniedFreq, longBuzzDuration);
        digitalWrite(redLedPin, LOW);
      }
    }
    mfrc522.PICC_HaltA();
    return;
  }

  if (irrecv.decode()) {
    uint32_t code = irrecv.decodedIRData.command;
    if (code == 0xFFFFFFFF || code == 0x0) {
      irrecv.resume();
      return;
    }

    int key = -1;
    switch (code) {
      case 0xC: key = 1; break;
      case 0x18: key = 2; break;
      case 0x5E: key = 3; break;
      case 0x8: key = 4; break;
      case 0x1C: key = 5; break;
      case 0x5A: key = 6; break;
      case 0x42: key = 7; break;
      case 0x52: key = 8; break;
      case 0x4A: key = 9; break;
      case 0x16: key = 0; break;
    }

    if (key != -1) {
      displayDigit(key);
      beep(digitBeepFreq, digitBeepDuration);
      passcodeInput += String(key);
      lastKeyPressTime = millis();

      if (passcodeInput.length() >= 4) {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("PROCESSING...");
        lcd.setCursor(0, 1);
        lcd.print("PLEASE WAIT");
        delay(1000);

        if (passcodeInput == correctPasscode) {
          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print("PIN VERIFIED!");
          lcd.setCursor(0, 1);
          lcd.print("Scan RFID Tag");
          digitalWrite(greenLedPin, HIGH);
          beep(highGrantedFreq, accessGrantedBeepDuration);
          delay(2000);
          digitalWrite(greenLedPin, LOW);
          rfidStage = true;
          attemptCount = 0;
        } else {
          attemptCount++;
          if (attemptCount >= 3) {
            lockedOut = true;
          } else {
            int attemptsLeft = 3 - attemptCount;
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("INCORRECT PIN");
            lcd.setCursor(0, 1);
            lcd.print(attemptsLeft);
            lcd.print(" left");
            digitalWrite(redLedPin, HIGH);
            beep(lowDeniedFreq, longBuzzDuration);
            digitalWrite(redLedPin, LOW);
          }
        }
        passcodeInput = "";
      }
    }
    irrecv.resume();
  }
}
