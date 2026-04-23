# 🛠️ Installatie – Solar Controller (ESP32)

Deze handleiding beschrijft de **volledige installatie** van de **Solar Controller**:  
van hardware aansluiten en eerste verbinding tot **integratie met externe systemen**.

De Solar Controller werkt **volledig lokaal** en stuurt een elektrische boiler **traploos via PWM** aan met behulp van een **Kemo M240**.

> ⚠️ **Waarschuwing**  
> Je werkt met **230V netspanning**. Schakel altijd de spanning uit en voer werkzaamheden aan netspanning alleen uit als je daarvoor bevoegd bent.

---

## 1️⃣ Benodigd

### Minimale werking
- ESP32 (bijv. DevKit-C V4)
- 230V → 5V DC voeding
- Kemo M240 vermogensregelaar
- Elektrische boiler met **analoge thermostaat**
- P1-meter (Youless LS120, HomeWizard P1 of smart gateway)
- WiFi-netwerk

Zie `docs/hardware.md` voor details.

---

## 2️⃣ Aansluiten – laagspanning

### 2.1 ESP32 voeding
Sluit de 5V voeding aan:

| Voeding | ESP32 |
|------|------|
| +5V | 5V / VIN |
| GND | GND |

Gebruik een **vaste 5V voeding** (min. 1A, bij voorkeur 2A).

---

### 2.2 PWM aansluiting (verplicht)

De Solar Controller gebruikt **één vaste PWM-pin**, vastgelegd in de firmware.

**PWM-pin:** `GPIO25`

**Aansluiten:**
- GPIO25 → PWM / CTRL ingang Kemo M240
- GND ESP32 → GND Kemo M240

**Belangrijk**
- PWM is **3.3V**
- **GND moet gedeeld zijn**
- Gebruik **niet** de 0–10V ingang van de Kemo
- PWM-frequentie: **1000 Hz**, resolutie **8-bit (0–255)**

---

## 3️⃣ Aansluiten – 230V

> ⚠️ Altijd spanningsloos werken.

Plaats de Kemo M240 **in serie** met de boiler:


Volg het officiële aansluitschema van de Kemo M240 en zorg voor:
- juiste draaddikte
- degelijke klemmen
- voldoende ventilatie

---

## 4️⃣ 1e keer Firmware flashen

Flash de Solar Controller firmware op de ESP32 via:
-  flash download tool met `first_intstall.bin` bestand
-  [First_install.zip](/first_install.zip)

Na flashen start de ESP32 automatisch opnieuw.  
PWM blijft **uit** tijdens opstart (boot-beveiliging).

---

## 5️⃣ Eerste keer verbinden (ZEER BELANGRIJK)

### Access Point modus
Bij de **eerste start**, of wanneer nog geen geldige WiFi-instellingen aanwezig zijn, start de Solar Controller automatisch in **Access Point (AP) modus**.

- De ESP32 maakt een eigen WiFi-netwerk aan
- Vast IP-adres:


**Stappen**
1. Verbind met het WiFi-netwerk van de Solar Controller
2. Open een browser en ga naar `http://192.168.4.1`
3. Stel je WiFi-netwerk en overige instellingen in
4. Sla op → de controller herstart automatisch

---

## 6️⃣ Normale werking (na WiFi-instelling)

Na herstart:
- Verbindt de Solar Controller met je thuisnetwerk
- Ontvangt een IP-adres van je router (DHCP)

Open daarna:

Het IP-adres vind je via:
- je router (DHCP-clients)
- de seriële monitor bij het flashen

---

## 7️⃣ `.local` adres (optioneel)

Na het instellen van een **Apparaatnaam** is de controller ook bereikbaar via:

- `http://devicename.local`

Voorbeeld:

- `http://solar-controller.local`

**Instellen:** via de webinterface bij **settings** / **Netwerk & Meter** 

> Werkt `.local` niet, gebruik dan het IP-adres van de controller.

---

## 8️⃣ P1-meter instellen

1. Zorg dat je P1-meter actief is in je netwerk
2. Vul het IP-adres en eventuele API-instellingen in via de webinterface
3. Controleer of **import- en exportvermogen** zichtbaar zijn

Zonder correcte P1-data werkt de regeling niet.

---

## 9️⃣ Temperatuursensoren (optioneel maar aanbevolen)

De Solar Controller ondersteunt **DS18B20 temperatuursensoren** via het 1-Wire protocol.

### Aansluiten
Alle sensoren worden **parallel** aangesloten.

| ESP32 | DS18B20 |
|-----|---------|
| **GPIO22** | DATA |
| **3.3V** | VCC |
| **GND** | GND |

Plaats een **4.7 kΩ pull-up weerstand** tussen **DATA (GPIO22)** en **3.3V**.

### Eigenschappen
- Meerdere sensoren op één pin
- Automatische detectie bij opstart
- Sensoren zichtbaar in webinterface en via MQTT
- Eén sensor kan worden ingesteld als **CONTROL-sensor**
- Gebruikt voor regeling en legionella-logica

Na aansluiten: **controller herstarten**.

---

## 10️⃣ In bedrijf nemen

1. Zet de boilerthermostaat niet direct op maximaal
2. Schakel de groep in
3. Controleer in het dashboard:
   - vermogensmetingen
   - PWM-regeling
   - temperaturen (indien aangesloten)
4. Controleer of Kemo en bekabeling niet overmatig warm worden

---

## 11️⃣ Integratie met externe systemen (optioneel)

De Solar Controller kan worden geïntegreerd met **meerdere systemen**, zoals:
- **Homey**
- **Home Assistant**
- **Node-RED**
- andere systemen die **MQTT** of **HTTP / REST** ondersteunen

### Architectuur
- De Solar Controller blijft **altijd zelfstandig functioneren**
- Externe systemen worden gebruikt voor:
  - visualisatie
  - bediening
  - automatiseringen
  - notificaties

### Communicatie
- **MQTT** (aanbevolen voor continue data)
- **HTTP / REST API** (voor status en acties)

### Wat externe systemen niet doen
- Geen realtime PWM-regeling
- Geen zonne-overschotberekening
- Geen kritische beslissingen

> Ook als een extern systeem offline is, blijft de Solar Controller normaal functioneren.

---

## Samenvatting

| Situatie | Adres |
|--------|-------|
| Eerste start / geen WiFi | `http://192.168.4.1` |
| Normale werking | `http://<DHCP-IP>` |
| Met device name | `http://<naam>.local` |

---

Einde installatie.
