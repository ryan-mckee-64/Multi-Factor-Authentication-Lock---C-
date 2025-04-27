/**************************************************************
 *  Includes
 **************************************************************/
#include <SPI.h>
#include <MFRC522.h>
#include <IRremote.hpp>

/**************************************************************
 *  IR Setup
 **************************************************************/
const uint16_t RECV_PIN = 2;  
IRrecv irrecv(RECV_PIN);

/**************************************************************
 *  Shift Register Pins
 **************************************************************/
const int dataPin  = 6;  // 74HC595 pin 14 (SER / DS)
const int latchPin = 7;  // 74HC595 pin 12 (RCLK / ST_CP)
const int clockPin = 8;  // 74HC595 pin 11 (SRCLK / SH_CP)

/**************************************************************
 *  7-Segment Patterns (Common-Anode)
 *  bit0=A, bit1=B, bit2=C, bit3=D, bit4=E, bit5=F, bit6=G, bit7=DP
 *  For a common-anode display, LOW (0) = segment ON.
 **************************************************************/
byte digitArray[10] = {
  0xC0, // 0
  0xF9, // 1
  0xA4, // 2
  0xA8, // 3 (swapped D/E)
  0x99, // 4
  0x8A, // 5 (swapped D/E)
  0x82, // 6
  0xF8, // 7
  0x80, // 8
  0x88  // 9 (swapped D/E)
};

/**************************************************************
 *  Buzzer (Software) Setup
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
bool rfidStage         = false;  // True once passcode is correct
bool lockoutBeepDone   = false;  // For the 6s lockout beep pattern

/**************************************************************
 *  LED Pins
 **************************************************************/
const int redLedPin    = A1;
const int yellowLedPin = A2;
const int greenLedPin  = A3;

/**************************************************************
 *  RFID Pins & Objects
 **************************************************************/
#define SS_PIN  10   // SDA on RC522
#define RST_PIN 9    // RST on RC522
MFRC522 mfrc522(SS_PIN, RST_PIN);

/**************************************************************
 *  ALLOWED RFID UID
 *  Only allow the RFID tag with UID: 00 64 63 82
 **************************************************************/
byte allowedUID[4] = { 0x00, 0x64, 0x63, 0x82 };

/**************************************************************
 *  CUSTOM BEEP FUNCTION (software-based)
 *  Avoids Timer2 conflict with IR
 **************************************************************/
void beep(int frequency, int durationMs) {
  unsigned long periodMicroSec = 1000000UL / frequency; 
  unsigned long totalCycles = (unsigned long)frequency * (unsigned long)durationMs / 1000UL;
  for (unsigned long i = 0; i < totalCycles; i++) {
    digitalWrite(buzzerPin, HIGH);
    delayMicroseconds(periodMicroSec / 2);
    digitalWrite(buzzerPin, LOW);
    delayMicroseconds(periodMicroSec / 2);
  }
}

/**************************************************************
 *  setup()
 **************************************************************/
void setup() {
  Serial.begin(115200);

  // Start IR
  irrecv.enableIRIn();  

  // Shift register pins
  pinMode(dataPin, OUTPUT);
  pinMode(latchPin, OUTPUT);
  pinMode(clockPin, OUTPUT);

  // Buzzer pin
  pinMode(buzzerPin, OUTPUT);
  digitalWrite(buzzerPin, LOW);

  // LED pins
  pinMode(redLedPin, OUTPUT);
  pinMode(yellowLedPin, OUTPUT);
  pinMode(greenLedPin, OUTPUT);
  digitalWrite(redLedPin, LOW);
  digitalWrite(yellowLedPin, LOW);
  digitalWrite(greenLedPin, LOW);

  // Clear 7-seg display at startup (all segments off = 0xFF for CA)
  digitalWrite(latchPin, LOW);
  shiftOut(dataPin, clockPin, MSBFIRST, 0xFF);
  digitalWrite(latchPin, HIGH);

  // Initialize RFID
  SPI.begin();
  mfrc522.PCD_Init(); 
  Serial.println("RC522 RFID reader initialized.");

  Serial.println("IR + RFID Lock System Ready. Awaiting IR signals...");
}

/**************************************************************
 *  loop()
 **************************************************************/
void loop() {
  // If permanently locked out, do the 6s beep pattern (once).
  if (lockedOut) {
    if (!lockoutBeepDone) {
      Serial.println("System Locked Out!");
      unsigned long startTime = millis();
      while (millis() - startTime < 6000) {
        digitalWrite(redLedPin, HIGH);
        beep(100, 500);
        digitalWrite(redLedPin, LOW);
        delay(500);
      }
      lockoutBeepDone = true;
    }
    return;
  }

  // If in RFID stage, ignore IR input and check for RFID scans.
  if (rfidStage) {
    checkRFID(); 
    return;
  }

  // Otherwise, process IR remote keypad input for passcode entry.
  if (irrecv.decode()) {
    uint32_t code = irrecv.decodedIRData.command;

    // Many remotes send 0xFFFFFFFF (NEC) or 0x0 for repeats.
    if (code == 0xFFFFFFFF || code == 0x0) {
      Serial.println("Repeat code, ignoring...");
      irrecv.resume();
      return;
    }

    Serial.print("Raw code: 0x");
    Serial.println(code, HEX);

    // Map IR codes to digits.
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
        Serial.print("Unknown IR code: 0x");
        Serial.println(code, HEX);
        break;
    }

    if (key != -1) {
      // Show digit on 7-seg display.
      displayDigit(key);
      // Quick beep for each digit.
      beep(digitBeepFreq, digitBeepDuration);
      // Build up passcode.
      passcodeInput += String(key);
      Serial.print("Digit pressed: ");
      Serial.println(key);
      Serial.print("Passcode so far: ");
      Serial.println(passcodeInput);

      // Check passcode when 4 digits entered.
      if (passcodeInput.length() >= 4) {
        if (passcodeInput == correctPasscode) {
          Serial.println("Access Granted - Now switching to RFID Stage");
          digitalWrite(greenLedPin, HIGH);
          beep(highGrantedFreq, accessGrantedBeepDuration);
          delay(gapBetweenGrantedBeeps);
          beep(highGrantedFreq, accessGrantedBeepDuration);
          digitalWrite(greenLedPin, LOW);

          digitalWrite(yellowLedPin, HIGH);  // Indicate RFID stage active.
          rfidStage = true;
          attemptCount = 0;
        } else {
          Serial.println("Access Denied");
          attemptCount++;
          if (attemptCount >= 3) {
            lockedOut = true;
          } else {
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
 *  checkRFID() - Called during RFID Stage.
 *  Scans for a new card. If the UID matches the allowed tag,
 *  access is granted.
 **************************************************************/
void checkRFID() {
  if (!mfrc522.PICC_IsNewCardPresent()) return;
  if (!mfrc522.PICC_ReadCardSerial())   return;

  // Compare the scanned UID with allowedUID.
  if (checkUidMatch(mfrc522.uid.uidByte, mfrc522.uid.size)) {
    Serial.println("RFID Tag Accepted!");
    digitalWrite(yellowLedPin, LOW);
    digitalWrite(greenLedPin, HIGH);
    beep(highGrantedFreq, accessGrantedBeepDuration);
    delay(gapBetweenGrantedBeeps);
    beep(highGrantedFreq, accessGrantedBeepDuration);
    // Green LED remains on indefinitely.
  } else {
    Serial.println("RFID Tag Denied!");
    digitalWrite(redLedPin, HIGH);
    beep(lowDeniedFreq, longBuzzDuration);
    digitalWrite(redLedPin, LOW);
    // Remain in RFID stage for additional attempts.
  }
  mfrc522.PICC_HaltA();
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
 *  displayDigit() - Show digit on 7-segment display.
 **************************************************************/
void displayDigit(int digit) {
  if (digit < 0 || digit > 9) return;
  byte segments = digitArray[digit];
  digitalWrite(latchPin, LOW);
  shiftOut(dataPin, clockPin, MSBFIRST, segments);
  digitalWrite(latchPin, HIGH);
}
