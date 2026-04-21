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
- Smart Gateways P1

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

## 5️⃣ Relais

### Doel
Naast PWM-uitgangen kan de Solar Controller ook een relais handmatig schakelen.

### Basis
- Relais gebruikt een vaste pin:
  - **GPIO18**

### Instellingen
- relais gebruiken: ja/nee
- relais type:
  - active LOW
  - active HIGH
- opstartstand:
  - uit
  - aan

### Gedrag
- Als **Relais gebruiken** uit staat, is de functie uitgeschakeld
- Als **Relais gebruiken** aan staat, verschijnt in het dashboard een handmatige aan/uit-bediening

### Toepassing
Dit relais kan worden gebruikt voor eenvoudige externe schakelingen die geen PWM nodig hebben.

---

## 6️⃣ Werkmodi & prioriteiten

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

## 7️⃣ Temperatuursensoren (DS18B20)

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

## 8️⃣ Temperatuurvrijgave controller

### Doel
Met **Temperatuurvrijgave controller** kan per controller worden bepaald of die wel of niet mag meedoen op basis van temperatuur.

### Instellingen per controller
- rekening houden met temperatuur: ja/nee
- temperatuursensor
- stoptemperatuur
- starttemperatuur

### Werking
- boven **stoptemperatuur** → controller doet niet mee
- onder **starttemperatuur** → controller mag weer meedoen
- tussen start en stop blijft de hysterese actief voor stabiel gedrag

### Belangrijk gedrag
Deze functie blokkeert alleen de **normale regeling / zonne-overschotregeling**.

Dus deze vrijgave blokkeert **niet**:
- Force Heat
- Legionella
- goedkope prijs
- negatieve prijs

Daar blijven de prioriteitsmodi gewoon werken.

### Lokaal en netwerk
- lokale controllers worden lokaal beoordeeld
- netwerk-controllers worden op de slave lokaal beoordeeld
- de master neemt een netwerk-controller alleen mee als die door de slave is vrijgegeven

### Controller 1
Controller 1 blijft ook hier altijd de vaste lokale uitgang op **GPIO25**.

---

## 9️⃣ Gas versus elektrisch verwarmen

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

## 🔟 Dynamische energieprijzen (Nordpool)

### Ondersteuning
- Vandaag en morgen
- Positieve en negatieve prijzen

### Gebruik
- Goedkoopste uren
- Negatieve prijzen
- Combinatie met zonne-overschot

Instellingen bepalen **hoe agressief** de controller reageert.

---

## 11️⃣ Multi Controller

### Doel
Met Multi Controller kunnen meerdere controllers samenwerken alsof ze één groep vormen.

Dit ondersteunt:
- meerdere boilers
- boiler + vloerverwarming
- meerdere PWM-uitgangen op één ESP
- combinatie van lokale en netwerk-controllers

### Basis
- Multi Controller kan **aan of uit**
- Uit = controller werkt weer **standalone lokaal**
- Aan = controller werkt als:
  - **Master**
  - of **Slave**

### Grenzen en opzet
- Maximaal **3 controllers totaal**
- **Controller 1** blijft altijd de vaste lokale uitgang op **GPIO25**
- Extra lokale PWM-uitgangen kunnen op:
  - **GPIO26**
  - **GPIO27**
  - **GPIO32**

### Algemene instellingen
- Multi Controller actief: ja/nee
- Werking zodra Multi Controller aan staat:
  - Master
  - Slave
- Aantal controllers in groep
- Eigen controller-ID
- Groepsnaam

### Controller-instellingen per slot
Per controller zijn instellingen mogelijk zoals:
- naam
- type:
  - lokale PWM-uitgang
  - netwerk-controller
- peer IP-adres
- lokale PWM-pin
- volgende mee vanaf PWM %
- volgorde opregelen
- volgorde terugregelen
- minimum PWM %
- maximum PWM %

### Scenario-instellingen per controller
Per controller kan worden ingesteld of die mag meedoen bij:
- zonne-overschot
- goedkope prijs
- negatieve prijs
- forceren / prioriteitswarmte

### Regelgedrag
Bij zonne-overschot werkt de groep sequentieel:
- eerst hogere prioriteit / eerdere controller
- daarna volgende controller volgens ingestelde vrijgave

Bij forceren of prioriteitsmodi:
- alle actieve controllers mogen meedoen binnen hun eigen limieten

### Belangrijke regel
Netwerk-controllers mogen alleen op de **laatste actieve positie(s)** staan.

Dus bijvoorbeeld:
- lokaal / lokaal / netwerk = goed
- lokaal / netwerk / netwerk = goed
- lokaal / netwerk / lokaal = niet toegestaan

### Veiligheid
Ook in Multi Controller blijft veiligheid altijd leidend:
- fasebeveiliging
- loadbalancing
- softstart
- min/max PWM
- andere bestaande limieten

### Master en slave
- **Master** bepaalt de groepsstrategie
- **Slave** voert opdrachten uit
- De slave blijft lokaal wel zijn eigen veiligheidslagen bewaken
- Bij verlies van de verbinding valt het systeem veilig terug

---

## 12️⃣ Externe systemen (optioneel)

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

## 13️⃣ Firmware & updates

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
