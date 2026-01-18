# 🚀 Eerste installatie – Solar Controller (ESP32 + Arduino IDE)

Deze handleiding beschrijft **stap voor stap** hoe je de **Solar Controller firmware** installeert op een **lege ESP32**, met behulp van de **Arduino IDE**.

👉 Dit is de **aanbevolen methode voor de eerste installatie**.  
👉 Geen andere tools nodig (geen Espressif flasher, geen ESPHome).

---

## 🔧 Benodigdheden

### Hardware
- ESP32 (bij voorkeur **ESP32 DevKit-C V4**)
- USB-kabel (**data-kabel**, geen “alleen opladen”)
- PC / laptop (Windows, macOS of Linux)

### Software
- **Arduino IDE 2.x**
- Internetverbinding

---

## 1️⃣ Arduino IDE installeren

Download en installeer Arduino IDE:

👉 https://www.arduino.cc/en/software

Start Arduino IDE na installatie.

---

## 2️⃣ ESP32 ondersteuning toevoegen

1. Ga naar **File → Preferences**
2. Plak bij **Additional Boards Manager URLs**: https://espressif.github.io/arduino-esp32/package_esp32_index.json
3. Klik **OK**
4. Ga naar **Tools → Board → Boards Manager**
5. Zoek op **ESP32**
6. Installeer **“esp32 by Espressif Systems”**

✅ Nu is Arduino IDE klaar voor ESP32-boards.

---

## 3️⃣ Benodigde Arduino libraries installeren

Ga naar:  
**Sketch → Include Library → Manage Libraries…**

### Altijd nodig
- **ArduinoJson** (v6.x)
- **PubSubClient**
- **OneWire**
- **DallasTemperature**
---

## 4️⃣ Board & poort instellen

### Board selecteren
Ga naar **Tools → Board** en kies: ESP32 Dev Module

❗ Gebruik géén andere ESP32-variant bij de eerste installatie.

### Poort selecteren
Ga naar **Tools → Port** en kies de COM-poort van je ESP32  
(bijvoorbeeld `COM3`, `COM4`, `/dev/ttyUSB0`).

Zie je geen poort?
- andere USB-kabel proberen
- andere USB-poort
- ESP32 loshalen en opnieuw aansluiten

---

## 5️⃣ Solar Controller firmware openen

1. Pak de Solar Controller firmware uit
2. Open het `.ino` bestand in Arduino IDE
3. Controleer:
   - juiste board geselecteerd
   - geen foutmeldingen in de editor

---

## 6️⃣ Flashen naar een lege ESP32

### (Aanbevolen) Flash eerst volledig wissen
Ga naar:
**Tools → Erase Flash → All Flash Contents**

➡️ Dit voorkomt problemen met oude Wi-Fi of instellingen.

### Uploaden
1. Klik op **Upload**
2. Blijft hij hangen op: Connecting........____
👉 Houd de **BOOT-knop** op de ESP32 ingedrukt
3. Laat BOOT los zodra het uploaden start
4. Wacht tot: Done uploading


---

## 7️⃣ Seriële monitor controleren (zeer belangrijk)

1. Ga naar **Tools → Serial Monitor**
2. Zet baudrate op **115200**
3. Druk op **RESET** (of USB eruit/in)

Je hoort nu opstart-informatie te zien, bijvoorbeeld:
- firmware start
- Wi-Fi initialisatie
- AP / setup mode

❗ Zie je niets of alleen herstarts?  
→ Boardselectie, libraries of flash is niet correct.

---

## 8️⃣ Eerste keer verbinden (Wi-Fi setup)

Bij de **eerste start**:

- De ESP32 start automatisch in **Wi-Fi setup mode**
- Hij maakt een **eigen Wi-Fi netwerk (Access Point)** aan

Zoek in je Wi-Fi lijst naar bijvoorbeeld:
- `SolarController`
- `Solar Controller`
- `SolarController-Setup`

### Verbinden
1. Verbind met dit Wi-Fi netwerk
2. Je wordt automatisch doorgestuurd naar de configuratiepagina
3. Vul je **eigen Wi-Fi gegevens** in
4. Opslaan → ESP32 herstart

Daarna:
- ESP32 verbindt met je eigen netwerk
- Webinterface is bereikbaar via:
- het IP-adres:  http://192.168.4.1


---

## 9️⃣ Zie je géén Wi-Fi netwerk?

Controleer dit in volgorde:

1. **Serial Monitor** → draait de firmware?
2. ESP32 ondersteunt **alleen 2.4 GHz Wi-Fi**
3. Wis flash opnieuw en upload opnieuw
4. Controleer:
- board = ESP32 Dev Module
- libraries geïnstalleerd
- baudrate = 115200

⚠️ “Upload geslaagd” betekent niet automatisch dat de firmware draait.

---

## ✅ Samenvatting

- Arduino IDE is **de beste tool** voor eerste installatie
- Altijd:
- juiste ESP32 board
- juiste libraries
- serial monitor checken
- Lege ESP32 start automatisch in **Wi-Fi setup mode**
- Geen Wi-Fi = firmware draait niet of crasht

---
