# ⚙️ Configuratie – Solar Controller

Dit document beschrijft **alle instellingen en opties** van de **Solar Controller**.  
De controller werkt **altijd zelfstandig**; configuratie bepaalt *hoe* en *wanneer* energie wordt benut.

Instellingen zijn beschikbaar via de **ingebouwde webinterface**.

---

## 🧭 Ontwerpprincipes

- Alles werkt **lokaal**
- Geen cloud-afhankelijkheid
- Instellingen zijn **transparant en uitlegbaar**
- Externe systemen (Homey, HA, Node-RED) zijn **optioneel**
- De Solar Controller blijft altijd het **regelbrein**

---

## 1️⃣ Algemene instellingen

### Device name
- Naam van de Solar Controller
- Wordt gebruikt voor:
  - Webinterface
  - MQTT topics
  - `.local` adres (mDNS)

Voorbeeld:

Bereikbaar via:

---

### Tijd & tijdzone
- Gebruikt voor:
  - logging
  - prijsvergelijking
  - dagovergangen
- Zorg dat tijdzone correct staat ingesteld

---

## 2️⃣ Netwerk & communicatie

### WiFi
- SSID en wachtwoord van je lokale netwerk
- Wijziging vereist herstart

---

### MQTT (optioneel maar aanbevolen)

MQTT is de **primaire integratielaag**.

#### Functies
- Publiceren van:
  - status
  - PWM-vermogen
  - temperaturen
  - actieve modus
  - gas/elektrisch advies
- Ontvangen van:
  - commando’s (bijv. Force Heat)

#### Instellingen
- Broker adres
- Poort
- Gebruikersnaam / wachtwoord (optioneel)
- Base topic (bijv. `solarcontroller/boiler`)

> Zonder MQTT blijft de Solar Controller volledig werken.

---

### HTTP / REST API
- Altijd beschikbaar
- Wordt gebruikt door:
  - webinterface
  - externe systemen
- Geschikt voor:
  - status uitlezen
  - acties uitvoeren
  - testen / debugging

---

## 3️⃣ Vermogensmeting (P1)

### P1-meter type
Ondersteund:
- Youless LS120
- HomeWizard P1

### Instellingen
- IP-adres van de P1-meter
- Eventuele API-parameters

### Gebruik
- Detectie van:
  - netafname
  - teruglevering
- Basis voor:
  - zonne-overschotregeling
  - nul-op-de-meter strategie

Zonder P1-data kan geen nauwkeurige regeling plaatsvinden.

---

## 4️⃣ Vermogensregeling (PWM)

### Basisprincipe
- De Solar Controller regelt **continu**
- Geen aan/uit gedrag
- PWM-waarde past zich aan op basis van:
  - gemeten vermogen
  - actieve modus
  - prioriteiten

### Veiligheid
- PWM = 0 bij:
  - opstarten
  - fouten
  - netwerkverlies (indien geconfigureerd)

---

## 5️⃣ Werkmodi & prioriteiten

De Solar Controller werkt met **prioriteiten**.  
Een hogere prioriteit **overrulet** lagere modi.

### Prioriteitsvolgorde (hoog → laag)
1. **Force Heat**
2. **Legionella-modus**
3. **Elektrisch verwarmen (prijs-gestuurd)**
4. **Zonne-overschot**
5. **Idle**

---

### Force Heat
- Dwingt verwarmen af
- Negeert:
  - zonne-overschot
  - prijzen
  - tijdsbeperkingen
- Handmatig of extern te activeren

Gebruik voor:
- direct warm water nodig
- testen

---

### Legionella-modus
- Periodiek of handmatig
- Verwarmt tot ingestelde minimumtemperatuur
- Gebruikt CONTROL-temperatuursensor

Kan:
- elektrisch
- of gecombineerd met overschot

---

### Elektrisch verwarmen (prijs-gestuurd)
- Vergelijkt:
  - gasprijs
  - elektriciteitsprijs
- Houdt rekening met:
  - rendement
  - belastingen
  - inkoopkosten
  - BTW

Wanneer elektriciteit goedkoper is:
- actief elektrisch verwarmen
- ook **zonder zonne-overschot**

---

### Zonne-overschot
- Standaard bedrijfsmodus
- Gebruikt alleen **overtollige energie**
- Minimaliseert teruglevering

---

### Idle
- Geen verwarming
- PWM = 0

---

## 6️⃣ Temperatuursensoren (DS18B20)

### Detectie
- Automatisch bij opstart
- Meerdere sensoren mogelijk

### Instellingen per sensor
- Naam
- Actief / inactief
- Eén sensor als **CONTROL-sensor**

### Gebruik CONTROL-sensor
- Regelstrategie
- Legionella
- Veiligheidslimieten

---

## 7️⃣ Gas versus elektrisch verwarmen

### Instelbaar
- Gasprijs (vast of dynamisch)
- Elektriciteitsprijs (Nordpool)
- Rendement gas / elektrisch
- Belastingen en BTW

### Resultaat
- Continu advies:
  - *Gas verwarmen*
  - *Elektrisch verwarmen*

### Automatisch uitvoeren
- Optioneel
- Heeft **hoge prioriteit**
- Wordt niet geblokkeerd door tijdschema’s

---

## 8️⃣ Dynamische energieprijzen (Nordpool)

### Ondersteuning
- Vandaag en morgen
- Positieve en negatieve prijzen

### Gebruik
- Goedkoopste uren
- Negatieve prijzen
- Combinatie met zonne-overschot

Instellingen bepalen **hoe agressief** de controller reageert.

---

## 9️⃣ Externe systemen (optioneel)

De Solar Controller kan worden geïntegreerd met:
- Homey
- Home Assistant
- Node-RED
- andere MQTT / REST systemen

### Rol van externe systemen
- Visualisatie
- Bediening
- Automatiseringen
- Meldingen

### Wat ze niet doen
- Geen realtime regeling
- Geen kernbeslissingen

> Bij uitval van externe systemen blijft de Solar Controller volledig functioneren.

---

## 10️⃣ Firmware & updates

### OTA updates
- Handmatig via webinterface
- Automatisch via GitHub (optioneel)

### Veiligheid
- Update alleen bij stabiele voeding
- Controller herstart automatisch

---

## 🧠 Tips & best practices

- Begin met alleen zonne-overschot
- Voeg prijssturing later toe
- Test Force Heat altijd zonder boiler
- Gebruik DHCP-reservering voor vast IP

---

Einde configuratie.
