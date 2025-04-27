/**************************************************************
 *  Includes
 **************************************************************/
#include <Wire.h>
#include <LiquidCrystal_I2C.h>  // For the I2C LCD
#include <SPI.h>
#include <MFRC522.h>
#include <IRremote.hpp>
#include <Servo.h>              // For the micro servo

/**************************************************************
 *  LCD Setup
 *  Adjust the address (0x27) if your module is different.
 **************************************************************/
LiquidCrystal_I2C lcd(0x27, 16, 2);

/**************************************************************
 *  IR Setup
 **************************************************************/
const uint16_t RECV_PIN = 2;  
IRrecv irrecv(RECV_PIN);

/**************************************************************
 *  Shift Register Pins (for 7-seg display)
 **************************************************************/
const int dataPin  = 6;   // 74HC595 pin 14 (SER/DS)
const int latchPin = 7;   // 74HC595 pin 12 (RCLK/ST_CP)
const int clockPin = 8;   // 74HC595 pin 11 (SRCLK/SH_CP)

/**************************************************************
 *  7-Segment Patterns (Common-Anode)
 **************************************************************/
byte digitArray[10] = {
  0xC0, // 0
  0xF9, // 1
  0xA4, // 2
  0xA8, // 3
  0x99, // 4
  0x8A, // 5
  0x82, // 6
  0xF8, // 7
  0x80, // 8
  0x88  // 9
};

/**************************************************************
 *  Buzzer Setup
 **************************************************************/
const int buzzerPin = 4; // Piezo buzzer on pin 4

/**************************************************************
 *  Frequencies & Durations
 **************************************************************/
const int digitBeepFreq            = 400;   // Hz
const int highGrantedFreq          = 3000;  // Hz
const int lowDeniedFreq            = 150;   // Hz

const int digitBeepDuration        = 100;   // ms
const int accessGrantedBeepDuration= 750;   // ms
const int gapBetweenGrantedBeeps   = 0;     // ms
const int longBuzzDuration         = 2250;  // ms

/**************************************************************
 *  Passcode Setup
 **************************************************************/
String passcodeInput    = "";
const String correctPasscode = "4747";
int attemptCount        = 0;
bool lockedOut          = false;

/**************************************************************
 *  RFID Stage + Lockout
 **************************************************************/
bool rfidStage         = false;  // True once PIN is verified
bool lockoutBeepDone   = false;  // For the initial 6s lockdown beep pattern
int rfidAttemptCount   = 0;      // RFID attempt counter

/**************************************************************
 *  LED Pins (ONLY Red and Green)
 **************************************************************/
const int redLedPin    = A1;
const int greenLedPin  = A3;

/**************************************************************
 *  RFID Pins & Objects
 **************************************************************/
#define SS_PIN  10   // SDA on RC522
#define RST_PIN 9    // RST on RC522
MFRC522 mfrc522(SS_PIN, RST_PIN);

/**************************************************************
 *  ALLOWED RFID UID
 *  Replace with your real UID if needed.
 **************************************************************/
byte allowedUID[4] = { 0x00, 0x64, 0x63, 0x82 };

/**************************************************************
 *  ULTRASONIC SENSOR Setup (HC-SR04)
 *  Trig -> Digital Pin 5, Echo -> Analog Pin A2.
 **************************************************************/
const int trigPin = 5;
const int echoPin = A2;

/**************************************************************
 *  PHOTO RESISTOR Setup
 *  The photo resistor is connected to Analog Pin A0.
 **************************************************************/
const int lightSensorPin = A0;
const int lightThreshold = 400;  // Lower threshold for light

/**************************************************************
 *  SERVO Setup
 *  Using TX pin (1) for the servo signal.
 **************************************************************/
Servo doorServo;

/**************************************************************
 *  POWER SAVING MODE Flag
 **************************************************************/
bool powerSaving = true;  // Initially, system is in power saving mode

/**************************************************************
 *  SLEEP MODE Flag
 **************************************************************/
bool sleepMode = false;   // Indicates if the system is in sleep mode

/**************************************************************
 *  Function: getDistance()
 *  Triggers the ultrasonic sensor and returns distance in cm.
 **************************************************************/
long getDistance() {
  long duration, distance;
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  duration = pulseIn(echoPin, HIGH, 30000); // timeout 30ms
  distance = duration * 0.0343 / 2;
  return distance;
}

/**************************************************************
 *  CUSTOM BEEP FUNCTION (quieter version)
 *  Uses a ~14% duty cycle.
 **************************************************************/
void beep(int frequency, int durationMs) {
  unsigned long periodMicroSec = 1000000UL / frequency;
  unsigned long onTime = periodMicroSec / 7; // ~14% duty cycle
  unsigned long offTime = periodMicroSec - onTime;
  unsigned long totalCycles = (unsigned long)frequency * (unsigned long)durationMs / 1000UL;
  for (unsigned long i = 0; i < totalCycles; i++) {
    digitalWrite(buzzerPin, HIGH);
    delayMicroseconds(onTime);
    digitalWrite(buzzerPin, LOW);
    delayMicroseconds(offTime);
  }
}

/**************************************************************
 *  SERVO MOVEMENT WITH SOUND
 *  Moves the servo from startAngle to endAngle in 1° steps.
 **************************************************************/
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

/**************************************************************
 *  UNLOCK DOOR SEQUENCE
 *  1) Display "Unlocking..." and rotate servo from 0° to 90°.
 *  2) Display "Enter Now, Have a great day!" for 5 seconds.
 *  3) Display "Locking..." and rotate servo back from 90° to 0°.
 *  4) Flash both red and green LEDs, then reset to standby mode.
 **************************************************************/
void unlockDoor() {
  // 1) "Unlocking..."
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Unlocking...");
  servoMoveWithSound(0, 90, 500, 20);

  // 2) "Enter Now..." for 5 seconds
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Enter Now,");
  lcd.setCursor(0,1);
  lcd.print("Have a great day!");
  delay(5000);

  // 3) "Locking..."
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Locking...");
  servoMoveWithSound(90, 0, 500, 20);

  // 4) Flash both LEDs a couple times
  for (int i = 0; i < 3; i++) {
    digitalWrite(redLedPin, HIGH);
    digitalWrite(greenLedPin, HIGH);
    delay(500);
    digitalWrite(redLedPin, LOW);
    digitalWrite(greenLedPin, LOW);
    delay(500);
  }
  
  // Reset state and return to standby mode
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("ON STANDBY");
  
  rfidStage = false;
  passcodeInput = "";
  attemptCount = 0;
  rfidAttemptCount = 0;
  powerSaving = true;
  
  // Detach servo so it can be reattached next cycle
  doorServo.detach();
}

/**************************************************************
 *  setup()
 **************************************************************/
void setup() {
  // No Serial initialization (TX used for servo)
  
  // Initialize IR receiver
  irrecv.enableIRIn();  

  // Initialize shift register pins
  pinMode(dataPin, OUTPUT);
  pinMode(latchPin, OUTPUT);
  pinMode(clockPin, OUTPUT);

  // Initialize buzzer pin
  pinMode(buzzerPin, OUTPUT);
  digitalWrite(buzzerPin, LOW);

  // Initialize LED pins
  pinMode(redLedPin, OUTPUT);
  pinMode(greenLedPin, OUTPUT);
  digitalWrite(redLedPin, LOW);
  digitalWrite(greenLedPin, LOW);

  // Clear 7-seg display (all segments off = 0xFF)
  digitalWrite(latchPin, LOW);
  shiftOut(dataPin, clockPin, MSBFIRST, 0xFF);
  digitalWrite(latchPin, HIGH);

  // Initialize RFID
  SPI.begin();
  mfrc522.PCD_Init(); 

  // Initialize LCD and show initial standby prompt ("ON STANDBY")
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("ON STANDBY");

  // Initialize ultrasonic sensor pins
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);

  // Initialize photo resistor pin
  pinMode(lightSensorPin, INPUT);

  // Attach servo to TX pin (1) and set starting position to 0°
  doorServo.attach(1);
  doorServo.write(0);
}

/**************************************************************
 *  loop()
 **************************************************************/
void loop() {
  // --- Check Light Level for Sleep/Operating Mode ---
  int lightVal = analogRead(lightSensorPin);
  // If dark, enter Sleep Mode.
  if (lightVal < lightThreshold) {
    if (!sleepMode) {
      sleepMode = true;
      // Reset system to standby
      rfidStage = false;
      passcodeInput = "";
      attemptCount = 0;
      rfidAttemptCount = 0;
      powerSaving = true;
      lcd.clear();
      lcd.setCursor(0,0);
      lcd.print("SLEEP MODE");
    }
    delay(500);
    return;
  } else {
    // If light is restored and we were in Sleep Mode, reset to standby.
    if (sleepMode) {
      sleepMode = false;
      rfidStage = false;
      passcodeInput = "";
      attemptCount = 0;
      rfidAttemptCount = 0;
      powerSaving = true;
      lcd.clear();
      lcd.setCursor(0,0);
      lcd.print("ON STANDBY");
      delay(1000);
    }
  }
  
  // --- POWER SAVING MODE ---
  if (powerSaving) {
    long dist = getDistance();
    if (dist > 0 && dist < 61) {  // Object detected within ~2 ft (61 cm)
      lcd.clear();
      lcd.setCursor(0,0);
      lcd.print("SCANNING USER");
      unsigned long scanStart = millis();
      while (millis() - scanStart < 4000) {
        // Signal processing beep: 1500 Hz for 50ms every 500ms.
        beep(1500, 50);
        delay(450);
      }
      // Transition to normal mode: PIN entry.
      lcd.clear();
      lcd.setCursor(0,0);
      lcd.print("Welcome User,");
      lcd.setCursor(0,1);
      lcd.print("Enter PIN Code");
      powerSaving = false;
    } else {
      lcd.clear();
      lcd.setCursor(0,0);
      lcd.print("ON STANDBY");
      delay(500);
      return;
    }
  }

  // --- LOCKDOWN MODE ---
  if (lockedOut) {
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("FULL SYSTEM");
    lcd.setCursor(0,1);
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
      lcd.setCursor(0,0);
      lcd.print("Lock resets in:");
      lcd.setCursor(0,1);
      lcd.print(i);
      delay(1000);
    }
    lockedOut = false;
    lockoutBeepDone = false;
    powerSaving = true;
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("ON STANDBY");
    return;
  }

  // --- RFID STAGE ---
  if (rfidStage) {
    if (!mfrc522.PICC_IsNewCardPresent()) return;
    if (!mfrc522.PICC_ReadCardSerial()) return;
    
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("PROCESSING...");
    lcd.setCursor(0,1);
    lcd.print("PLEASE WAIT");
    delay(1000);
    
    if (checkUidMatch(mfrc522.uid.uidByte, mfrc522.uid.size)) {
      lcd.clear();
      lcd.setCursor(0,0);
      lcd.print("RFID TAG OK!");
      lcd.setCursor(0,1);
      lcd.print("WELCOME USER");
      digitalWrite(greenLedPin, HIGH);
      beep(highGrantedFreq, accessGrantedBeepDuration);
      delay(gapBetweenGrantedBeeps);
      beep(highGrantedFreq, accessGrantedBeepDuration);
      digitalWrite(greenLedPin, LOW);
      unlockDoor();
    } else {
      rfidAttemptCount++;
      if (rfidAttemptCount >= 3) {
        lockedOut = true;
      } else {
        int attemptsLeft = 3 - rfidAttemptCount;
        lcd.clear();
        lcd.setCursor(0,0);
        lcd.print("INCORRECT RFID");
        lcd.setCursor(0,1);
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

  // --- PIN ENTRY via IR Keypad ---
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
        lcd.setCursor(0,0);
        lcd.print("PROCESSING...");
        lcd.setCursor(0,1);
        lcd.print("PLEASE WAIT");
        delay(1000);
        if (passcodeInput == correctPasscode) {
          lcd.clear();
          lcd.setCursor(0,0);
          lcd.print("PIN VERIFIED!");
          lcd.setCursor(0,1);
          lcd.print("Scan RFID Tag");
          digitalWrite(greenLedPin, HIGH);
          beep(highGrantedFreq, accessGrantedBeepDuration);
          delay(gapBetweenGrantedBeeps);
          beep(highGrantedFreq, accessGrantedBeepDuration);
          digitalWrite(greenLedPin, LOW);
          delay(2000);  // Extra pause before RFID stage
          // Reattach servo for unlocking sequence
          doorServo.attach(1);
          rfidStage = true;
          attemptCount = 0;
        } else {
          attemptCount++;
          if (attemptCount >= 3) {
            lockedOut = true;
          } else {
            int attemptsLeft = 3 - attemptCount;
            lcd.clear();
            lcd.setCursor(0,0);
            lcd.print("INCORRECT PIN");
            lcd.setCursor(0,1);
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

/**************************************************************
 *  checkUidMatch()
 *  Compare the scanned UID to our allowedUID.
 **************************************************************/
bool checkUidMatch(byte *uid, byte uidSize) {
  if (uidSize != 4) return false;
  for (byte i = 0; i < 4; i++) {
    if (uid[i] != allowedUID[i]) {
      return false;
    }
  }
  return true;
}

/**************************************************************
 *  displayDigit()
 *  Show a digit on the 7-seg display.
 **************************************************************/
void displayDigit(int digit) {
  if (digit < 0 || digit > 9) return;
  byte segments = digitArray[digit];
  digitalWrite(latchPin, LOW);
  shiftOut(dataPin, clockPin, MSBFIRST, segments);
  digitalWrite(latchPin, HIGH);
}
