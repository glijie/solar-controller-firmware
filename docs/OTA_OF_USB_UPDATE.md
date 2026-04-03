# 🔄 OTA of USB installatie / update – Solar Controller

De Solar Controller ondersteunt **2 installatieroutes**:

- **OTA**
- **USB**

Welke route je gebruikt, hangt af van de manier waarop jouw ESP32 eerder is geïnstalleerd en of er voldoende geheugen beschikbaar is voor OTA-updates.

---

## 🧭 Uitgangspunt

De **OTA-route** blijft de eenvoudigste manier van updaten.  
Voor ESP32's met te weinig vrije ruimte voor OTA-updates is er ook een **USB-route** beschikbaar.

---

## 1️⃣ OTA installatie / updates

Gebruik de OTA-route als jouw controller via de **webinterface** werkt en OTA-updates ondersteunt.

### Bestanden
- **OTA_first_install.zip** → voor de eerste installatie --> gebruik flash-adress 0x0000
- **SolarController.bin** → voor volgende updates --> gebruik flash-adress 0x10000

### Belangrijk
Na de eerste OTA-installatie kunnen volgende updates gewoon via de webinterface van de controller uitgevoerd worden.

Daarvoor is geen:
- USB-kabel
- flashtool

nodig.

---

## 2️⃣ USB installatie / updates

Gebruik de USB-route als OTA niet werkt of als jouw ESP32 te weinig geheugen heeft voor OTA-updates.

### Bestanden
- **USB_First_install.zip** → voor de eerste installatie --> gebruik flash-adress 0x0000
- **USB_Update.bin** → voor volgende updates --> gebruik flash-adress 0x10000

### Belangrijk
Een USB-update moet altijd uitgevoerd worden met een **flashtool**, zoals bijvoorbeeld **Flash Download Tool**.

Hiervoor moet de ESP32 met een **USB-kabel** aangesloten worden op de computer.

---

## 3️⃣ Welke route moet ik kiezen?

### Kies OTA als:
- jouw controller al via OTA werkt
- je update via de webinterface
- jouw ESP32 voldoende ruimte heeft voor OTA

### Kies USB als:
- OTA niet werkt
- jouw ESP32 te weinig geheugen heeft voor OTA
- je handmatig wilt flashen via USB

---

## 4️⃣ Let op

Gebruik altijd de bestanden die horen bij jouw eigen installatiemethode:

- gebruik je **OTA**, gebruik dan de **OTA-bestanden**
- gebruik je **USB**, gebruik dan de **USB-bestanden**

Twijfel je welke route jij gebruikt?

Dan geldt meestal:

- update je via de **webinterface van de controller** → **OTA**
- update je met een **USB-kabel en flashtool** → **USB**

---
