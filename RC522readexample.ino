/**************************************************************
 *  Simple RC522 Example
 *  Reads and prints the UID of any presented RFID tag/card
 **************************************************************/
#include <SPI.h>
#include <MFRC522.h>

/**************************************************************
 *  RC522 Pin Definitions
 **************************************************************/
#define SS_PIN  10   // SDA on RC522
#define RST_PIN 9    // RST on RC522

MFRC522 mfrc522(SS_PIN, RST_PIN); // Create MFRC522 instance

void setup() {
  Serial.begin(9600);
  SPI.begin();           // Init SPI bus
  mfrc522.PCD_Init();    // Init MFRC522
  Serial.println("RC522 RFID reader ready.");
  Serial.println("Please place your RFID tag/card near the reader...");
}

void loop() {
  // If no new card is present, do nothing
  if (!mfrc522.PICC_IsNewCardPresent()) {
    return;
  }
  
  // If the card can't be read, do nothing
  if (!mfrc522.PICC_ReadCardSerial()) {
    return;
  }

  // Print the UID (Unique Identifier)
  Serial.print("Card UID: ");
  for (byte i = 0; i < mfrc522.uid.size; i++) {
    // Print each byte of the UID in hex (with leading 0 if < 0x10)
    if (mfrc522.uid.uidByte[i] < 0x10) {
      Serial.print("0");
    }
    Serial.print(mfrc522.uid.uidByte[i], HEX);
    if (i < mfrc522.uid.size - 1) {
      Serial.print(" ");
    }
  }
  Serial.println();

  // Print the SAK (Select Acknowledge), just for completeness
  Serial.print("SAK: ");
  Serial.println(mfrc522.uid.sak, HEX);

  // Halt PICC (stop reading this card)
  mfrc522.PICC_HaltA();
}

