# 🔧 Minimale benodigde hardware – Solar Controller

Dit document beschrijft de **minimale hardware** die nodig is om de  
**Solar Controller (ESP32)** functioneel en betrouwbaar te laten werken.

Deze configuratie is voldoende voor:
- het detecteren van zonne-overschot
- het traploos aansturen van een elektrische boiler
- volledig lokaal gebruik (geen cloud)
- gebruik via de ingebouwde webinterface

---

## ✅ Verplichte onderdelen

### 1️⃣ ESP32 microcontroller
De kern van de Solar Controller.

**Aanbevolen:**
- ESP32 DevKit-C V4  
  (of een gelijkwaardige ESP32 met WiFi)

**Eisen:**
- WiFi
- 3.3V logica
- PWM-ondersteuning

---

### 2️⃣ 5V DC voeding
Voor het voeden van de ESP32.

**Specificaties:**
- Ingang: 230V AC
- Uitgang: 5V DC
- Stroom: minimaal 1A (2A aanbevolen)

> De voeding vervangt de originele 12V voeding die bij sommige oudere opstellingen werd gebruikt. Zoals Arduino 0-10V aansturing

---

### 3️⃣ Vermogensregelaar – Kemo M240
Voor het traploos aansturen van het verwarmingselement.

**Belangrijk:**
- De Kemo M240 wordt **aangestuurd via PWM**
- **Geen 0–10V regeling**
- GND van ESP32 en Kemo **moet gedeeld worden**

**Functie:**
- Regelt het vermogen naar de boiler op basis van zonne-overschot of prijsstrategie
---
### Hardwarebeveiliging – Kemo M240

De Kemo M240 mag nooit boven zijn specificaties worden belast.

De Kemo M240 is bedoeld voor maximaal:

- **10 A**
- **2300 W**

Daarom moet de installatie elektrisch worden beveiligd.

#### Vereist
- Gebruik een passende automaat / zekering vóór de Kemo
- Kies de beveiliging zodanig dat de Kemo niet boven **10 A / 2300 W** kan worden belast
- Stel in de Solar Controller de **Max PWM %** zo in dat het werkelijk opgenomen vermogen binnen deze veilige grens blijft bij grotere vermogens dan 10A en 2300W

#### Belangrijk
De instelling **Max PWM %** is een extra softwarematige begrenzing, maar vervangt geen elektrische beveiliging.

De elektrische installatie moet altijd zo zijn uitgevoerd dat de Kemo M240 ook bij foutieve instellingen, verkeerde configuratie of defecten niet boven zijn specificaties belast kan worden.

---

### 4️⃣ Elektrische boiler
De verbruiker die aangestuurd wordt.

**Vereisten:**
- 230V
- Mechanische / analoge thermostaat
- Vast vermogen (bijv. 1500W – 3000W)

❌ Niet geschikt:
- Boilers met digitale vermogensregeling
- Slimme boilers met eigen elektronica

---

### 5️⃣ P1-meter (voor vermogensmeting)
Nodig om import / export van het net te meten.

**Getest en ondersteund:**
- ✔️ Youless LS120
- ✔️ HomeWizard P1 Meter
- ✔️ Smart Gateways P1

**Gebruik:**
- Detectie van zonne-overschot
- Minimaliseren van teruglevering
- Realtime vermogensregeling

De P1-meter communiceert via het lokale netwerk met de ESP32.

---

### 6️⃣ Netwerk (WiFi)
De Solar Controller werkt volledig lokaal, maar heeft netwerk nodig voor:

- Webinterface
- P1-meter uitlezen
- (optioneel) MQTT
- OTA firmware-updates

**Let op:**
- Zorg voor voldoende WiFi-bereik bij de boilerlocatie

---

## 🔌 Basis bekabeling

Minimaal nodig:
- 230V bedrading (boiler ↔ Kemo M240)
- Laagspanningsbedrading:
  - 5V voeding → ESP32
  - PWM signaal → Kemo M240
  - GND gedeeld tussen ESP32 en Kemo
- Kroonstenen of lasklemmen

---

## ⚠️ Belangrijke aandachtspunten

- De Solar Controller stuurt **PWM**, geen 0–10V
- GND van alle componenten **moet gemeenschappelijk zijn**
- Werk altijd spanningsloos bij aansluiten
- 230V installaties alleen uitvoeren als je daarvoor bevoegd bent

---

## ℹ️ Wat is *niet* verplicht?

De volgende onderdelen zijn **optioneel** en niet nodig voor basiswerking:
- Temperatuursensoren (DS18B20)
- Home Assistant
- Node-RED
- Behuizing (sterk aanbevolen, maar niet technisch verplicht)

---
