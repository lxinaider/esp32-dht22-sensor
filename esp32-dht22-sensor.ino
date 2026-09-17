#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include "DHT.h"

#define DHTPIN 16
#define DHTTYPE DHT22 

DHT dht(DHTPIN, DHTTYPE);

LiquidCrystal_I2C lcd(/*addr=*/0x27, /*cols=*/16, /*rows=*/2);

void setup() {
  Serial.begin(9600);
  
  dht.begin();
  
  Wire.begin(/*SDA=*/21, /*SCL=*/22); 
  lcd.init();
  lcd.backlight();
  
  lcd.setCursor(0, 0);
  lcd.print("Iniciando...");
  delay(2000);
  lcd.clear();
}

void loop() {
  delay(2000);

  float h = dht.readHumidity();
  float t = dht.readTemperature();

  lcd.clear();

  if (isnan(h) || isnan(t)) {
    lcd.setCursor(0, 0);
    lcd.print("Erro no Sensor!");
    return;
  }

  lcd.setCursor(0, 0);
  lcd.print("Temp: ");
  lcd.print(t, 1);
  lcd.print((char)223); // Símbolo de graus (°) ou 1101 1111
  lcd.print("C");

  lcd.setCursor(0, 1);
  lcd.print("Umid: ");
  lcd.print(h, 1);
  lcd.print("%");
}