# ☀️ Solar Controller (ESP32)

**Solar Controller** is een ESP32-gebaseerde controller voor het **slim benutten van zonne-overschot**.  
Het systeem stuurt verbruikers (zoals een elektrische boiler) **traploos** aan op basis van **actueel vermogen**, **energieprijzen** en **instellingen**, volledig lokaal en zonder cloud.

**Doel:**
- Maximaal eigen verbruik  
- Minimale teruglevering  
- Volledig automatisch, maar transparant en instelbaar  

---

## 🔧 Wat doet de Solar Controller?

- Gebruikt live P1-meterdata om **zonne-overschot** te detecteren  
- Stuurt een vermogensregelaar **traploos via PWM** aan  
- Combineert overschot met **Nordpool dynamische prijzen**  
- Bepaalt zelfstandig wanneer en hoeveel vermogen gebruikt wordt  
- Biedt een **ingebouwde webinterface** (dashboard + instellingen)  
- Ondersteunt **OTA firmware-updates via GitHub**  

---

## ✨ Belangrijkste functies

### ⚡ Slim energiegebruik
- Continue vermogensregeling (geen aan/uit relaisgedrag)  
- Direct reageren op overschot of tekort  

**Geschikt voor:**
- Elektrische boilers  
- Verwarmingselementen  
- Andere resistieve verbruikers  

---

### 📊 Dynamische energieprijzen (Nordpool)

De Solar Controller ondersteunt **Nordpool energieprijzen**.

**Aanwezig en actief:**
- Prijzen voor **vandaag en morgen**  
- Ondersteuning voor:
  - Positieve prijzen  
  - **Negatieve prijzen**  
- Prijsinformatie wordt actief gebruikt in de regelstrategie:
  - Goedkoopste uren  
  - Negatieve uren  
  - Combinatie van prijs + zonne-overschot  

Dit maakt het mogelijk om verbruik slim te verschuiven en optimaal gebruik te maken van dynamische tarieven.

---

### ⚡ P1-meter compatibiliteit (bewezen)

De Solar Controller werkt met de volgende P1-meters:

#### ✔️ Youless LS120
- Actueel vermogen (import/export)  
- Zeer stabiel  
- Veel gebruikt in combinatie met de Solar Controller  

#### ✔️ HomeWizard P1 Meter
- Actueel vermogen  
- Betrouwbare realtime data  
- Direct inzetbaar  

Deze data wordt gebruikt om:
- Zonne-overschot te detecteren  
- Teruglevering te minimaliseren  
- Vermogen nauwkeurig te regelen  

---

## 🌐 Webinterface (standalone)

De Solar Controller heeft een **volledig ingebouwde webinterface**.

**Bevat:**
- Dashboard met status en actuele waarden  
- Instellingenpagina  
- Firmware / OTA update pagina  

Werkt op:
- PC  
- Tablet  
- Smartphone  

Geen externe software nodig.

---

## 🔌 API & integratie

### 🌐 HTTP API (REST)

De Solar Controller beschikt over een **lokale HTTP API**.

**Gebruikt voor:**
- Dashboard & webinterface  
- Configuratie  
- Statusinformatie  
- Firmware-updates  

**API-functies:**
- Uitlezen van status en systeeminformatie  
- Aanpassen en opslaan van instellingen  
- Ophalen van firmware-informatie  
- Tonen van update-voortgang  
- Detecteren wanneer het systeem na reboot weer online is  

De API is lokaal bereikbaar via het IP-adres van de Solar Controller.

---

### 🔁 MQTT (actief gebruikt)

MQTT is volledig geïntegreerd.

**Functies:**
- Ontvangen van:
  - Actueel vermogen  
  - Nordpool prijsinformatie  
- Publiceren van:
  - Status  
  - Actieve modus  
  - PWM / regelwaarde  

Geschikt voor:
- Home Assistant  
- Node-RED  
- Monitoring & logging  

---

### 🏠 Home Assistant integratie (actief)

De Solar Controller wordt actief gebruikt met **Home Assistant**.

**Werkt met:**
- P1-meter data  
- Nordpool prijsinformatie  
- Dashboards en automatiseringen  

De Solar Controller blijft hierbij:
- Zelfstandig beslissen  
- Realtime reageren  
- Volledig lokaal functioneren  

---

## 🔧 Vermogensregeling

**Aanwezige functionaliteit:**
- PWM-uitgang  
- Traploze regeling  

**Geschikt voor vermogensregelaars zoals:**
- **Kemo M240**

**Toepassingen:**
- Elektrische boiler  
- Verwarmingselement  
- Andere resistieve belastingen  

> ⚠️ Werken met 230V vereist kennis en zorgvuldigheid.

---

## 🔄 Firmware & OTA updates

**Aanwezig:**
- OTA updates via GitHub  
- Handmatige update via webinterface  
- Automatische update (instelbaar)  
- Update-voortgang zichtbaar:
  - Statusmeldingen  
  - Voortgangsbalk  
- Automatische reboot  
- Automatische terugkeer naar dashboard  

De controller controleert:
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

- Geen cloud afhankelijkheid  
- Alles lokaal  
- Transparant gedrag  
- Geen black box  
- Gebouwd voor stabiliteit en dagelijks gebruik  
- Gericht op maximaal eigen verbruik  

---

## 📜 Disclaimer

Dit project is bedoeld voor **educatief en eigen gebruik**.  
Gebruik is op eigen risico. De auteur is niet aansprakelijk voor schade door foutieve installatie of gebruik.

---

## ❤️ motivatie

Dit project is ontstaan uit de wens om:
- Slimmer met energie om te gaan  
- Onafhankelijk te zijn van cloud-diensten  
- Zonne-energie écht optimaal te benutten  
