#include <LiquidCrystal_I2C.h>
#include <SPI.h>
#include <MFRC522.h>
#include <IRremote.hpp>
#include <Servo.h>

// Initialize LCD (16x2) at address 0x27
LiquidCrystal_I2C lcd(0x27, 16, 2);

// IR receiver on pin 2
const uint16_t RECV_PIN = 2;
IRrecv irrecv(RECV_PIN);

// Shift register pins for 7-segment display
// Updated assignments: Data (SER) -> pin 8, Latch (RCLK) -> pin 7, Clock (SRCLK) -> pin 6
const int dataPin = 8;
const int latchPin = 7;
const int clockPin = 6;

// New 7‑segment digit patterns for a common‑cathode display,
// where the bits (0 = A, 1 = B, 2 = C, 3 = D, 4 = E, 5 = F, 6 = G)
// are shifted left by 1 so that they occupy outputs Q1..Q7.
// According to your wiring (common tied to ground):
//   QA (Q0) is unused (grounded)
//   QB (Q1) → Segment A
//   QC (Q2) → Segment B
//   QD (Q3) → Segment C
//   QE (Q4) → Segment D
//   QF (Q5) → Segment E
//   QG (Q6) → Segment F
//   QH (Q7) → Segment G
byte digitArray[10] = {
  0x7E, // 0: 0x3F << 1 = 0x7E (A, B, C, D, E, F on; G off)
  0x0C, // 1: 0x06 << 1 = 0x0C (B, C on)
  0xB6, // 2: 0x5B << 1 = 0xB6 (A, B, D, E, G on)
  0x9E, // 3: 0x4F << 1 = 0x9E (A, B, C, D, G on)
  0xCC, // 4: 0x66 << 1 = 0xCC (B, C, F, G on)
  0xDA, // 5: 0x6D << 1 = 0xDA (A, C, D, F, G on)
  0xFA, // 6: 0x7D << 1 = 0xFA (A, C, D, E, F, G on)
  0x0E, // 7: 0x07 << 1 = 0x0E (A, B, C on)
  0xFE, // 8: 0x7F << 1 = 0xFE (All segments on)
  0xDE  // 9: 0x6F << 1 = 0xDE (A, B, C, D, F, G on)
};

// Buzzer on pin 3 (updated)
const int buzzerPin = 3;

// Beep frequencies and durations
const int digitBeepFreq = 400;
const int highGrantedFreq = 3000;
const int lowDeniedFreq = 150;
const int digitBeepDuration = 100;
const int accessGrantedBeepDuration = 750;
const int gapBetweenGrantedBeeps = 0;
const int longBuzzDuration = 2250;

// Passcode and lock variables
String passcodeInput = "";
const String correctPasscode = "4747";
int attemptCount = 0;
bool lockedOut = false;

// RFID and lockout variables
bool rfidStage = false;
bool lockoutBeepDone = false;
int rfidAttemptCount = 0;

// LED pins for red and green indicators
const int redLedPin = A1;
const int greenLedPin = A3;

// RFID reader setup (pins 10 for SDA and 9 for RST)
#define SS_PIN 10
#define RST_PIN 9
MFRC522 mfrc522(SS_PIN, RST_PIN);
byte allowedUID[4] = { 0x00, 0x64, 0x63, 0x82 };

// Ultrasonic sensor pins (HC-SR04)
// Trig remains on digital pin 5, Echo updated to A0
const int trigPin = 5;
const int echoPin = A0;

// Photo resistor (via voltage divider) on A2 (updated)
const int lightSensorPin = A2;
const int lightThreshold = 400;  // Adjusted threshold

// Servo for door control on digital pin 4 (updated)
Servo doorServo;

// Mode flags
bool powerSaving = true;
bool sleepMode = false;

// Get distance from ultrasonic sensor in centimeters
long getDistance() {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  long duration = pulseIn(echoPin, HIGH, 30000);
  return duration * 0.0343 / 2;
}

// Custom beep with ~14% duty cycle
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

// Move servo step by step with beep at each step
void servoMoveWithSound(int startAngle, int endAngle, int freq, int stepTimeMs) {
  if (startAngle < endAngle) {
    for (int angle = startAngle; angle <= endAngle; angle++) {
      doorServo.write(angle);
      beep(freq, stepTimeMs);
    }
  } else {
    for (int angle = startAngle; angle >= endAngle; angle--) {
      doorServo.write(angle);
      beep(freq, stepTimeMs);
    }
  }
}

// Door unlock sequence with LCD prompts, servo movement, and LED flashing
void unlockDoor() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Unlocking...");
  servoMoveWithSound(0, 90, 500, 20);
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Enter Now,");
  lcd.setCursor(0, 1);
  lcd.print("Have a great day!");
  delay(5000);
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Locking...");
  servoMoveWithSound(90, 0, 500, 20);
  
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
  doorServo.detach();
}

void setup() {
  irrecv.enableIRIn();
  pinMode(dataPin, OUTPUT);
  pinMode(latchPin, OUTPUT);
  pinMode(clockPin, OUTPUT);
  pinMode(buzzerPin, OUTPUT);
  digitalWrite(buzzerPin, LOW);
  pinMode(redLedPin, OUTPUT);
  pinMode(greenLedPin, OUTPUT);
  digitalWrite(redLedPin, LOW);
  digitalWrite(greenLedPin, LOW);
  
  digitalWrite(latchPin, LOW);
  shiftOut(dataPin, clockPin, MSBFIRST, 0xFF);
  digitalWrite(latchPin, HIGH);
  
  SPI.begin();
  mfrc522.PCD_Init();
  
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("ON STANDBY");
  
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  pinMode(lightSensorPin, INPUT);
  
  doorServo.attach(4); // Updated servo pin to digital pin 4
  doorServo.write(0);
}

void loop() {
  // Only check sleep mode if in power saving mode
  if (powerSaving) {
    int lightVal = analogRead(lightSensorPin);
    if (lightVal < lightThreshold) {
      if (!sleepMode) {
        sleepMode = true;
        rfidStage = false;
        passcodeInput = "";
        attemptCount = 0;
        rfidAttemptCount = 0;
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("SLEEP MODE");
      }
      delay(500);
      return;
    } else {
      if (sleepMode) {
        sleepMode = false;
        rfidStage = false;
        passcodeInput = "";
        attemptCount = 0;
        rfidAttemptCount = 0;
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("ON STANDBY");
        delay(1000);
      }
    }
  }
  
  // When in power saving mode, scan for a user
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
      powerSaving = false;  // Disable sleep mode checks after user is scanned
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
    lockoutBeepDone = false;
    powerSaving = true;
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("ON STANDBY");
    return;
  }
  
  if (rfidStage) {
    if (!mfrc522.PICC_IsNewCardPresent()) return;
    if (!mfrc522.PICC_ReadCardSerial()) return;
    
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
      delay(gapBetweenGrantedBeeps);
      beep(highGrantedFreq, accessGrantedBeepDuration);
      digitalWrite(greenLedPin, LOW);
      delay(2000);
      // Call the unlock sequence which shows "Unlocking..." and moves the servo.
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
      case 0xC:   key = 1; break;
      case 0x18:  key = 2; break;
      case 0x5E:  key = 3; break;
      case 0x8:   key = 4; break;
      case 0x1C:  key = 5; break;
      case 0x5A:  key = 6; break;
      case 0x42:  key = 7; break;
      case 0x52:  key = 8; break;
      case 0x4A:  key = 9; break;
      case 0x16:  key = 0; break;
      default:
        break;
    }
    
    if (key != -1) {
      displayDigit(key);
      beep(digitBeepFreq, digitBeepDuration);
      passcodeInput += String(key);
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
          delay(gapBetweenGrantedBeeps);
          beep(highGrantedFreq, accessGrantedBeepDuration);
          digitalWrite(greenLedPin, LOW);
          delay(2000);
          // Prepare for RFID scanning
          doorServo.attach(4);
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

bool checkUidMatch(byte *uid, byte uidSize) {
  if (uidSize != 4) return false;
  for (byte i = 0; i < 4; i++) {
    if (uid[i] != allowedUID[i]) return false;
  }
  return true;
}

void displayDigit(int digit) {
  if (digit < 0 || digit > 9) return;
  byte segments = digitArray[digit];
  digitalWrite(latchPin, LOW);
  shiftOut(dataPin, clockPin, MSBFIRST, segments);
  digitalWrite(latchPin, HIGH);
}
