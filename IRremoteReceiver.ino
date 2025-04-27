#include <IRremote.hpp>

const uint16_t RECV_PIN = 2;  // IR receiver signal connected to pin 12 on Arduino
IRrecv irrecv(RECV_PIN);

// Variables for debounce logic
unsigned long lastKeyTime = 0;        // Timestamp of the last processed key press
uint32_t lastKey = 0;                 // Last IR command processed
const unsigned long debounceDelay = 300; // Debounce delay in milliseconds

void setup() {
  Serial.begin(115200);
  while (!Serial);  // Wait for Serial Monitor to open
  irrecv.enableIRIn();  // Start the IR receiver
  Serial.println("IR Keypad Mode: Awaiting signals...");
}

void loop() {
  if (irrecv.decode()) {
    uint32_t code = irrecv.decodedIRData.command;
    unsigned long currentTime = millis();
    
    // Process the key if it's a new press or enough time has passed (debounce)
    if ((currentTime - lastKeyTime > debounceDelay) || (code != lastKey)) {
      lastKey = code;
      lastKeyTime = currentTime;
      
      int key = -1; // Default: -1 indicates an unknown key
      
      // Map the raw IR command codes to keypad numbers
      switch (code) {
        case 0xC:  // IR code for '1'
          key = 1;
          break;
        case 0x18:  // IR code for '2'
          key = 2;
          break;
        case 0x5E:  // IR code for '3'
          key = 3;
          break;
        case 0x8:  // IR code for '4'
          key = 4;
          break;
        case 0x1C:  // IR code for '5'
          key = 5;
          break;
        case 0x5A:  // IR code for '6'
          key = 6;
          break;
        case 0x42:  // IR code for '7'
          key = 7;
          break;
        case 0x52:  // IR code for '8'
          key = 8;
          break;
        case 0x4A:  // IR code for '9'
          key = 9;
          break;
        case 0x16:  // IR code for '0'
          key = 0;
          break;
        default:
          Serial.print("Unknown IR code: 0x");
          Serial.println(code, HEX);
          break;
      }
      
      // If a valid key was detected, print the number
      if (key != -1) {
        Serial.println(key);
      }
    }
    irrecv.resume();  // Prepare to receive the next IR code
  }
}
