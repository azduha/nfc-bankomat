#include <Int64String.h>

#include <PN5180.h>
#include <PN5180ISO14443.h>

#include <LiquidCrystal_I2C.h>
#include <Wire.h>

#define DEBUG

#define PN5180_NSS  10
#define PN5180_BUSY 9
#define PN5180_RST  8

LiquidCrystal_I2C lcd(0x27,  16, 2);
PN5180ISO14443 nfc(PN5180_NSS, PN5180_BUSY, PN5180_RST);

int readDataBlock(PN5180ISO14443 nfc, uint8_t *blockData, uint8_t blockSize) {
  uint8_t blocks = blockSize >> 4;
  for (uint8_t i = 0; i < blocks; i++) {
    if (readSingleBlock(nfc, i * 4 + 0x04, blockData + 16 * i, 16) == 0) {
      return 0;
    }
  }
  return 1;
}

int readSingleBlock(PN5180ISO14443 nfc, uint8_t blockNo, uint8_t *blockData, uint8_t blockSize) {
  uint8_t buffer[8];

  uint8_t cmd[7];
	uint8_t uidLength = 0;
	// Load standard TypeA protocol
	if (!nfc.loadRFConfig(0x0, 0x80)) 
	  return 0;

	// OFF Crypto
	if (!nfc.writeRegisterWithAndMask(SYSTEM_CONFIG, 0xFFFFFFBF))
	  return 0;
	// Clear RX CRC
	if (!nfc.writeRegisterWithAndMask(CRC_RX_CONFIG, 0xFFFFFFFE))
	  return 0;
	// Clear TX CRC
	if (!nfc.writeRegisterWithAndMask(CRC_TX_CONFIG, 0xFFFFFFFE))
	  return 0;
	//Send REQA/WUPA, 7 bits in last byte
  cmd[0] = 0x52;
	if (!nfc.sendData(cmd, 1, 0x07))
	  return 0;
	// READ 2 bytes ATQA into  buffer
	if (!nfc.readData(2, buffer))
	  return 0;
  
  //Enable RX CRC calculation
	if (!nfc.writeRegisterWithOrMask(CRC_RX_CONFIG, 0x01)) {
    // Serial.println("Setup error 1!");
	  return 0;
  }
	//Enable TX CRC calculation
	if (!nfc.writeRegisterWithOrMask(CRC_TX_CONFIG, 0x01)) {
    // Serial.println("Setup error 2!");
	  return 0;
  }
  
  uint8_t readSingleBlock[] = { 0x30, blockNo };

  if (nfc.sendData(readSingleBlock, sizeof(readSingleBlock), 0x00) == 0) {
    // Serial.println("Send error!");
    return 0;
  }
  if (nfc.readData(blockSize, blockData) == 0) {
    // Serial.println("Read error!");
    return 0;
  }

  uint8_t halt[] = { 0x50, 0x00 };

  nfc.sendData(halt, sizeof(halt), 0x00);

  return 1;
}

void setup() {
  // Serial.begin(115200);

  lcd.init();
  lcd.noBacklight();
  lcd.clear();

  nfc.begin();
  nfc.reset();
  nfc.setupRF();
}

bool errorFlag = false;
char buff[16];

uint64_t lastCardNum = 0;

void loop() {

  uint32_t irqStatus = nfc.getIRQStatus();
  
  uint8_t uid[8];
  uint8_t rc = nfc.readCardSerial(uid);

  if (rc == 0) {
    if (!errorFlag) {
      lcd.clear();
      lcd.noBacklight();
      lcd.setCursor(0,0);
      lcd.print("Prilozte kartu");
      lastCardNum = 0;
      errorFlag = true;
    }
    delay(1000);
    return;
  }

  lcd.backlight();

  errorFlag = false;

  uint64_t cardNum = 0;

  for (int i=0; i<7; i++) {
    cardNum *= 256;
    cardNum += uid[i];
  }

  if (lastCardNum == cardNum) {
    delay(1000);
    return;
  }

  lastCardNum = cardNum;

  String cardNumStr = int64String(cardNum);

  uint8_t data[48];

  uint8_t result = readDataBlock(nfc, data, sizeof(data));

  if (!result) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Chyba cteni karty!");
    delay(1000);
    return;
  }

  // for (int i = 0; i < sizeof(data); i++) {
  //   if (i % 8 == 0) {
  //     Serial.println();
  //     Serial.print(i, HEX);
  //     Serial.print(":    ");
  //   }
  //   Serial.print(data[i], HEX);
  //   Serial.print(" ");
  //   Serial.print((char)data[i]);
  //   Serial.print("      ");
  // }
  // Serial.println();

  float balance;
  memcpy(&balance, data + 13, 4);

  char balStr[14];
  dtostrf(balance, 14, 2, balStr);
  // Serial.println(balStr);

  uint8_t name_len = data[0x13];

  char name[name_len + 1];
  name[name_len] = 0x00;

  bool nameError = false;

  for (uint8_t i = 0; i < name_len; i++) {
    name[i] = data[0x17 + i];
    if (data[0x17 + i] == 0xFF) {
      nameError = true;
    }
  }

  if (nameError || balance != balance || balance < 0 || balance > 1000000000000000.0) {
    // Check for NaN
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Karta predcasne");
    lcd.setCursor(0, 1);
    lcd.print("odebrana!");
    delay(1000);
    return;
  }

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(name);

  lcd.setCursor(0, 1);
  lcd.print(balStr);
  lcd.print(F(" $"));

  errorFlag = false;
  delay(1000);

  nfc.reset();
  nfc.setupRF();
}
