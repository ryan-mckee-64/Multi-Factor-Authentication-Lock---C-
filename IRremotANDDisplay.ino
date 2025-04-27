#include <IRremote.hpp>  // Make sure you have the IRremote library installed

// ------------------- IR Setup -------------------
const uint16_t RECV_PIN = 2;  // IR receiver signal pin
IRrecv irrecv(RECV_PIN);

// Debounce variables
unsigned long lastKeyTime = 0;
uint32_t lastKey = 0;
const unsigned long debounceDelay = 300; // milliseconds

// ------------------- Shift Register Pins -------------------
const int dataPin  = 6;  // 74HC595 pin 14 (SER / DS)
const int latchPin = 7;  // 74HC595 pin 12 (RCLK / ST_CP)
const int clockPin = 8;  // 74HC595 pin 11 (SRCLK / SH_CP)

// ------------------- 7-Segment Patterns (Common-Anode) -------------------
// Bit mapping: bit0 = A, bit1 = B, bit2 = C, bit3 = D, bit4 = E, bit5 = F, bit6 = G, bit7 = DP
// For a common-anode display, a LOW (0) turns a segment ON.
// The following array is adjusted so that the physical wiring where the D and E segments are swapped 
// will display digits correctly.
// Standard patterns (for correct wiring) vs. modified patterns below:
//   3: from 0xB0 to 0xA8, 5: from 0x92 to 0x8A, 9: from 0x90 to 0x88.
byte digitArray[10] = {
  0xC0, // 0: A, B, C, D, E, F on; G off
  0xF9, // 1: B, C on
  0xA4, // 2: A, B, D, E, G on
  0xA8, // 3: A, B, C, D, G on (modified: swap D and E bits)
  0x99, // 4: B, C, F, G on
  0x8A, // 5: A, C, D, F, G on (modified: swap D and E bits)
  0x82, // 6: A, C, D, E, F, G on
  0xF8, // 7: A, B, C on
  0x80, // 8: All segments on except DP
  0x88  // 9: A, B, C, D, F, G on (modified: swap D and E bits)
};

void setup() {
  Serial.begin(115200);
  while (!Serial);

  // Initialize IR
  irrecv.enableIRIn();  // Start the IR receiver

  // Initialize shift register pins
  pinMode(dataPin, OUTPUT);
  pinMode(latchPin, OUTPUT);
  pinMode(clockPin, OUTPUT);

  // Clear the display at startup (all segments off = 0xFF for CA)
  digitalWrite(latchPin, LOW);
  shiftOut(dataPin, clockPin, MSBFIRST, 0xFF);
  digitalWrite(latchPin, HIGH);

  Serial.println("IR + 7-Segment (Common-Anode) Demo: Awaiting signals...");
}

void loop() {
  // Check if we have received an IR code
  if (irrecv.decode()) {
    uint32_t code = irrecv.decodedIRData.command;
    unsigned long currentTime = millis();

    // Simple debounce check
    if ((currentTime - lastKeyTime > debounceDelay) || (code != lastKey)) {
      lastKey = code;
      lastKeyTime = currentTime;

      int key = -1; // -1 indicates unknown

      // Map IR codes to digits 0-9 (based on your previous code)
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

      // If we got a valid digit, display it
      if (key != -1) {
        Serial.print("Digit pressed: ");
        Serial.println(key);
        displayDigit(key);
      }
    }

    // Resume IR reception for the next code
    irrecv.resume();
  }
}

// ------------------- Helper Function -------------------
// Shifts out the correct bit pattern to show 'digit' on the 7-segment
void displayDigit(int digit) {
  // Safety check
  if (digit < 0 || digit > 9) return;

  byte segments = digitArray[digit];

  // Send bits to the shift register
  digitalWrite(latchPin, LOW);
  shiftOut(dataPin, clockPin, MSBFIRST, segments);
  digitalWrite(latchPin, HIGH);
}
