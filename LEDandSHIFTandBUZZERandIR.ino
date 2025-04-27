#include <IRremote.hpp>

// ------------------- IR Setup -------------------
const uint16_t RECV_PIN = 2;  
IRrecv irrecv(RECV_PIN);

// ------------------- Shift Register Pins -------------------
const int dataPin  = 6;  // 74HC595 pin 14 (SER / DS)
const int latchPin = 7;  // 74HC595 pin 12 (RCLK / ST_CP)
const int clockPin = 8;  // 74HC595 pin 11 (SRCLK / SH_CP)

// ------------------- 7-Segment Patterns (Common-Anode) -------------------
// bit0=A, bit1=B, bit2=C, bit3=D, bit4=E, bit5=F, bit6=G, bit7=DP
// For a common-anode display, LOW (0) = segment ON.
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

// ------------------- Buzzer (Software) Setup -------------------
const int buzzerPin = 4; // Piezo buzzer on pin 4

// ------------------- Frequencies & Durations -------------------
const int digitBeepFreq            = 400;   // Hz
const int highGrantedFreq          = 3000;  // Hz
const int lowDeniedFreq            = 150;   // Hz

const int digitBeepDuration        = 100;   // ms
const int accessGrantedBeepDuration= 750;   // ms
const int gapBetweenGrantedBeeps   = 0;     // ms
const int longBuzzDuration         = 2250;  // ms

// ------------------- Passcode Setup -------------------
String passcodeInput    = "";
const String correctPasscode = "4747";
int attemptCount        = 0;
bool lockedOut          = false;

// ------------------- RFID Stage Flag -------------------
bool rfidStage = false;

// ------------------- Lockout Beep Flag -------------------
bool lockoutBeepDone = false;

// ------------------- LED Pins -------------------
const int redLedPin    = A1;
const int yellowLedPin = A2;
const int greenLedPin  = A3;

// ----------------------------------------------------------------------
// CUSTOM BEEP FUNCTION (software-based, avoids Timer2 conflict with IR)
// ----------------------------------------------------------------------
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

  // Clear display at startup (all segments off = 0xFF for CA)
  digitalWrite(latchPin, LOW);
  shiftOut(dataPin, clockPin, MSBFIRST, 0xFF);
  digitalWrite(latchPin, HIGH);

  Serial.println("IR Passcode Entry Demo (No Timer Conflict). Awaiting signals...");
}

void loop() {
  // If permanently locked out, beep on and off for 6 seconds with flashing Red LED.
  if (lockedOut) {
    if (!lockoutBeepDone) {
      Serial.println("System Locked Out!");
      unsigned long startTime = millis();
      while (millis() - startTime < 6000) {
        // Turn red LED on and beep for 500ms
        digitalWrite(redLedPin, HIGH);
        beep(100, 500);
        // Turn red LED off and pause for 500ms
        digitalWrite(redLedPin, LOW);
        delay(500);
      }
      lockoutBeepDone = true;
    }
    return;
  }
  
  // If in RFID stage, ignore further keypad inputs.
  if (rfidStage) {
    // Place RFID logic here if needed.
    return;
  }

  // Process IR remote keypad input only if not in RFID stage.
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
      case 0xC:   key = 1; break;  // '1'
      case 0x18:  key = 2; break;  // '2'
      case 0x5E:  key = 3; break;  // '3'
      case 0x8:   key = 4; break;  // '4'
      case 0x1C:  key = 5; break;  // '5'
      case 0x5A:  key = 6; break;  // '6'
      case 0x42:  key = 7; break;  // '7'
      case 0x52:  key = 8; break;  // '8'
      case 0x4A:  key = 9; break;  // '9'
      case 0x16:  key = 0; break;  // '0'
      default:
        Serial.print("Unknown IR code: 0x");
        Serial.println(code, HEX);
        break;
    }

    if (key != -1) {
      // Display the digit on the 7-seg display.
      displayDigit(key);

      // Quick beep for each digit press.
      beep(digitBeepFreq, digitBeepDuration);

      // Build up the passcode string.
      passcodeInput += String(key);
      Serial.print("Digit pressed: ");
      Serial.println(key);
      Serial.print("Passcode so far: ");
      Serial.println(passcodeInput);

      // When 4 digits are entered, check the passcode.
      if (passcodeInput.length() >= 4) {
        if (passcodeInput == correctPasscode) {
          Serial.println("Access Granted");

          // Turn on Green LED during the access granted beeps.
          digitalWrite(greenLedPin, HIGH);
          beep(highGrantedFreq, accessGrantedBeepDuration);
          delay(gapBetweenGrantedBeeps);
          beep(highGrantedFreq, accessGrantedBeepDuration);
          // Turn off Green LED after beeps.
          digitalWrite(greenLedPin, LOW);

          // Transition to RFID stage by turning on Yellow LED continuously.
          digitalWrite(yellowLedPin, HIGH);

          // Disable further keypad input.
          rfidStage = true;

          // Reset attempt count.
          attemptCount = 0;
        } else {
          Serial.println("Access Denied");
          attemptCount++;
          if (attemptCount >= 3) {
            lockedOut = true;
          } else {
            // For access denied, flash Red LED during the buzzer sound.
            digitalWrite(redLedPin, HIGH);
            beep(lowDeniedFreq, longBuzzDuration);
            digitalWrite(redLedPin, LOW);
          }
        }
        // Clear passcode input for next attempt.
        passcodeInput = "";
      }
    }
    // Prepare for the next IR signal.
    irrecv.resume();
  }
}

// ------------------- Helper Function: Display Digit -------------------
void displayDigit(int digit) {
  if (digit < 0 || digit > 9) return;
  byte segments = digitArray[digit];
  digitalWrite(latchPin, LOW);
  shiftOut(dataPin, clockPin, MSBFIRST, segments);
  digitalWrite(latchPin, HIGH);
}
