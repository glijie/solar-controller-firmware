# ☀️ Solar Controller (ESP32)

**Solar Controller** is een ESP32-gebaseerde controller voor het **slim en lokaal benutten van zonne-overschot**.  
De controller stuurt verbruikers (zoals een elektrische boiler) **traploos** aan op basis van **actueel vermogen**, **energieprijzen** en **instellingen** — volledig lokaal, zonder cloudafhankelijkheid.

### Doelstellingen
- Maximaal eigen verbruik  
- Minimale (of nul) teruglevering  
- Volledig automatisch, maar transparant en instelbaar  
- Geschikt voor dagelijks en langdurig gebruik  

---

## 🔧 Wat doet de Solar Controller?

- Leest realtime **P1-meterdata** (import / export)
- Detecteert zonne-overschot en netafname
- Stuurt een vermogensregelaar **traploos via PWM**
- Combineert overschot met **Nordpool dynamische energieprijzen**
- Bepaalt zelfstandig:
  - *wanneer* er verbruikt wordt  
  - *hoeveel* vermogen gebruikt wordt  
- Beschikt over een **volledig ingebouwde webinterface**
- Ondersteunt **OTA firmware-updates via GitHub**

---

## ✨ Kernfunctionaliteit

### ⚡ Slim energiegebruik
- Continue vermogensregeling (geen relais-achtig aan/uit gedrag)
- Reageert direct op veranderingen in:
  - zonne-productie
  - huishoudelijk verbruik
  - netimport / netexport

**Geschikt voor:**
- Elektrische boilers  
- Verwarmingselementen  
- Andere resistieve belastingen  

---

### 🌡️ Temperatuursensoren (DS18B20)

De Solar Controller ondersteunt **meerdere temperatuursensoren** via 1-Wire.

**Eigenschappen**
- Meerdere DS18B20-sensoren op één datapin
- Automatische detectie
- Per sensor instelbaar:
  - Naam
  - Actief / inactief
- Eén sensor kan worden aangewezen als **CONTROL-sensor**
  - gebruikt voor regeling en legionella-logica

**Integratie**
- Temperatuur zichtbaar in:
  - Solar Controller dashboard
  - Home Assistant (via MQTT Discovery)
- Sensor-namen die in de Solar Controller worden ingesteld,
  worden automatisch overgenomen in Home Assistant

---

### 📊 Dynamische energieprijzen (Nordpool)

Ondersteuning voor **Nordpool spotprijzen** is volledig geïntegreerd.

**Ondersteund**
- Prijzen voor vandaag en morgen
- Positieve én negatieve prijzen
- Gebruik in regelstrategie:
  - goedkoopste uren
  - negatieve uren
  - combinatie van prijs + zonne-overschot

Hiermee kan verbruik slim worden verschoven naar de economisch meest gunstige momenten.

---

### ⚡ P1-meter compatibiliteit (bewezen)

Geteste en gebruikte P1-meters:

#### ✔️ Youless LS120
- Actueel vermogen (import / export)
- Zeer stabiel
- Veel gebruikt in combinatie met de Solar Controller

#### ✔️ HomeWizard P1 Meter
- Realtime vermogensdata
- Direct inzetbaar

**Gebruik**
- Detectie van zonne-overschot
- Minimaliseren van teruglevering
- Nauwkeurige vermogensregeling

---

## 🌐 Webinterface (standalone)

De Solar Controller bevat een **volledig geïntegreerde webinterface**.

**Functionaliteit**
- Dashboard met:
  - actuele status
  - vermogensregeling
  - temperaturen
- Instellingenpagina
- Firmware / OTA-updatepagina

**Toegang**
- PC
- Tablet
- Smartphone  

Geen externe software of cloud nodig.

---

## 🔌 API & integraties

### 🌐 HTTP API (REST)

Lokale HTTP-API voor:

- Webinterface
- Statusinformatie
- Configuratie
- Firmware-updates

**Ontwerp**
- Scheiding tussen:
  - lichte status-API (dashboard)
  - volledige status- en configuratie-API
- Gericht op stabiliteit en lage geheugenbelasting

---

### 🔁 MQTT

MQTT is een **kernonderdeel** van het systeem.

**Ontvangen**
- Actueel vermogen
- Nordpool prijsinformatie

**Publiceren**
- Status
- Actieve modus
- Vermogensregelwaarde (PWM)
- Temperaturen

Geschikt voor:
- Home Assistant
- Node-RED
- Logging en monitoring

---

### 🏠 Home Assistant integratie

De Solar Controller integreert naadloos met **Home Assistant**.

**Kenmerken**
- MQTT Discovery
- Automatische entity-aanmaak
- Sensor-namen worden overgenomen vanuit de Solar Controller
- Home Assistant kan visualiseren en automatiseren  
- De Solar Controller blijft **zelfstandig beslissen**

---

## 🔧 Vermogensregeling

**Aanwezig**
- PWM-uitgang
- Traploze regeling

**Compatibel met**
- **Kemo M240**
- Andere PWM-gestuurde vermogensregelaars

**Toepassingen**
- Elektrische boiler
- Verwarmingselement
- Andere resistieve belastingen

---

## 🔄 Firmware & OTA updates

**Ondersteund**
- OTA updates via GitHub
- Handmatige update via webinterface
- Automatische update (instelbaar)
- Volledige voortgangsindicatie
- Automatische reboot en terugkeer naar dashboard

**Update-controle**
- Bij opstarten
- Periodiek (± elke 30 minuten)

---

## 🚀 Installatie (globaal)

1. Flash de firmware (`.bin`) op een ESP32  
2. Start de ESP32  
3. Verbind met het WiFi-setup portal  
4. Open de webinterface  
5. Configureer:
   - Netwerk
   - Vermogensregeling
   - Databronnen
   - Update-instellingen  
6. Klaar ✔️

---

## 🧠 Ontwerpfilosofie

- Geen cloudafhankelijkheid
- Alles lokaal
- Transparant gedrag
- Geen black-box logica
- Ontworpen voor:
  - stabiliteit
  - langdurig gebruik
  - uitbreidbaarheid

---

## 📜 Disclaimer

Dit project is bedoeld voor **educatief en eigen gebruik**.  
Gebruik is op eigen risico. De auteur is niet aansprakelijk voor schade door foutieve installatie of gebruik.

---

## ❤️ Motivatie

De Solar Controller is ontstaan uit de wens om:
- Slimmer met energie om te gaan
- Onafhankelijk te zijn van cloud-diensten
- Zonne-energie **écht optimaal** te benutten
