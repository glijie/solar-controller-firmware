# Solar Controller Homey App 

Release notes voor de Homey app update voor Solar Controller

## Wat zit erin

Deze update brengt de Homey app weer gelijk met de actuele Solar Controller firmware en voegt ondersteuning toe voor meerdere ESP controllers in dezelfde Homey installatie.

## Nieuwe Multi-ESP ondersteuning

Vanaf deze versie kunnen meerdere Solar Controllers als losse Homey apparaten worden toegevoegd.

Elke gekoppelde ESP krijgt:

- een eigen Homey device;
- een eigen Host/IP instelling;
- een eigen polling- en performanceprofiel;
- eigen tegels, flows en statuswaarden.

Dit maakt het mogelijk om meerdere boilers, Kemo/PWM controllers of Solar Controller installaties naast elkaar te gebruiken.

## Apparaat tegels

De Homey apparaatweergave bevat de belangrijkste waarden van de Solar Controller:

- Vermogen
- Temperatuur
- PWM
- Max % PWM
- Regelmodus
- Zonregeling
- Relaisstatus
- Legionella actief
- Legionella status
- Multi Controller rol
- Multi Controller fallback
- Group PWM
- Peers online
- Peers healthy
- TCP realtime
- Temperatuurvrijgave status
- Extra temperaturen 2, 3 en 4
- Elektraprijs
- Gasprijs
- Verwarmingsadvies

De namen van de regelmodus sluiten aan op de benamingen in de Solar Controller webinterface.

## Bediening in Homey

De bedieningspagina bevat directe bediening voor:

- Force heat
- Relais handmatig
- Zonregeling
- Max % PWM controller
- Legionella aan/uit

Voor zonregeling zijn de volgende modi beschikbaar:

- Uit
- Altijd
- Handmatig venster
- Goedkoopste daglicht

De `Max % PWM controller` bediening gebruikt een bereik van `0` tot `100` en stuurt deze waarde als percentage naar de Solar Controller.

## Solar Controller API ondersteuning

De app gebruikt de actuele Solar Controller API endpoints:

- `/api/status`
- `/api/status_light`
- `/api/live`
- `/api/heat_compare`
- `/api/config`
- `/api/force_heat`
- `/api/relay`
- `/api/relay/on`
- `/api/relay/off`
- `/api/legionella/run_now`
- `/api/legionella/cancel`

Ook oudere firmware met `/api/pwm` blijft bruikbaar. Als directe PWM sturing niet beschikbaar is, gebruikt de app een fallback via max output en Force heat.

## Flow kaarten

Deze release bevat:

- 22 triggers
- 13 condities
- 9 acties

### Acties

- Force heat aan
- Force heat uit
- Relais aan
- Relais uit
- Zet zonregeling op modus
- Zet max PWM percentage
- Zet PWM percentage
- Legionella nu starten
- Legionella annuleren

### Condities

- Vermogen boven waarde
- PWM boven waarde
- Temperatuur boven waarde
- Legionella is actief
- Verwarmingsadvies is
- Elektraprijs onder waarde
- Gasprijs onder waarde
- Zonregeling is
- Regelmodus is
- Multi Controller rol is
- Multi Controller fallback is actief
- Relais is aan
- Temperatuurvrijgave blokkeert

### Triggers

- Vermogen gewijzigd
- PWM gewijzigd
- Temperatuur gewijzigd
- Temperatuur 2 gewijzigd
- Temperatuur 3 gewijzigd
- Temperatuur 4 gewijzigd
- Force heat aan
- Force heat uit
- Legionella aan
- Legionella uit
- Advies gewijzigd
- Elektraprijs bijgewerkt
- Gasprijs bijgewerkt
- Regelmodus gewijzigd
- Zonregeling gewijzigd
- Relais aan
- Relais uit
- Multi Controller rol gewijzigd
- Multi Controller fallback actief
- Multi Controller fallback voorbij
- Temperatuurvrijgave blokkeert
- Temperatuurvrijgave vrijgegeven

## Performance instellingen

De app bevat instellingen om de belasting op Homey te regelen:

- Performance mode: auto, normal of legacy
- Polling interval
- HTTP timeout
- Max gelijktijdige API requests
- Update alleen bij echte verandering
- Drempels voor vermogen, PWM, temperatuur en prijzen
- Optioneel pollen van heat-compare data
- Extra temperatuursensoren aan/uit

Voor oudere Homey hardware kan `legacy` gebruikt worden om rustiger te pollen.


## Compatibiliteit

Bestaande gebruikers hoeven hun apparaten niet opnieuw te koppelen. Nieuwe capabilities en instellingen worden bij app-start gemigreerd.

# installatie Homey app via App store (test)

[klik Hier](https://homey.app/nl-nl/app/com.patrick.solarcontroller/Solar-Controller/test/)


```


```
---

# installatie Homey app via CLI

## Windows – CMD

Onderstaande stappen zijn bedoeld voor gebruikers die de Solar Controller Homey app handmatig via de Homey CLI installeren.

### Eerste installatie

#### 1. Installeer Node.js LTS

Download en installeer de actuele LTS-versie van Node.js:

[Node.js downloaden](https://nodejs.org/en/download)

Na de installatie kan CMD opnieuw worden geopend.

#### 2. Open CMD

Open in Windows:

**Opdrachtprompt / Command Prompt**

Dit kan bijvoorbeeld via:

**Start → typ `CMD` → Enter**

#### 3. Installeer de Homey CLI

Voer in CMD uit:

```cmd
npm install -g homey
```

Dit hoeft normaal gesproken alleen de eerste keer uitgevoerd te worden.

#### 4. Log in op Homey

Voer daarna uit:

```cmd
homey login
```

Volg de instructies om in te loggen op het Homey-account waarop de app geïnstalleerd moet worden.

#### 5. Download en pak de app ZIP uit

Download de release ZIP en pak deze uit naar een vaste map op de computer.

Bijvoorbeeld:

```text
C:\Homey\Solar_Controller_Homey_v0.7.73
```

#### 6. Ga in CMD naar de app-map

Ga naar de map waarin de Homey app is uitgepakt.

Bijvoorbeeld:

```cmd
cd C:\Homey\Solar_Controller_Homey_v0.7.73
```

Belangrijk: je moet in de map staan waarin onder andere het bestand `app.json` aanwezig is.

Dus niet in de map waar alleen het ZIP-bestand staat.

Je kunt controleren of je in de juiste map staat met:

```cmd
dir
```

In de lijst moet `app.json` zichtbaar zijn.

#### 7. Installeer de dependencies

Voer uit:

```cmd
npm install
```

Hiermee worden de benodigde Node.js dependencies voor de Homey app geïnstalleerd.

#### 8. Installeer de app op Homey

Voer vervolgens uit:

```cmd
homey app install
```

De Homey CLI bouwt de app en installeert deze vervolgens op de gekoppelde Homey.

Na een succesvolle installatie kan de Solar Controller via **Homey → Apparaten → Nieuw apparaat** worden toegevoegd of verder worden gebruikt.

---

## Bestaande installatie updaten

Wanneer al een eerdere versie van de Solar Controller Homey app via de CLI is geïnstalleerd, zijn er minder stappen nodig.

Download eerst de nieuwe ZIP en pak deze uit.

Open vervolgens CMD en ga naar de map van de nieuwe versie:

```cmd
cd C:\pad\naar\app-map
```

Waarbij dit de map is waarin het nieuwe `app.json` bestand staat.

Voer daarna uit:

```cmd
npm install
homey app install
```

De bestaande Homey app wordt hiermee bijgewerkt naar de nieuwe versie.

Bestaande gekoppelde Solar Controller apparaten hoeven daarbij normaal gesproken niet opnieuw te worden toegevoegd.

### Kort overzicht voor updates

```cmd
cd C:\pad\naar\app-map
npm install
homey app install
```

Gebruik bij iedere nieuwe release altijd de map van de **nieuwe uitgepakte versie** van de Homey app.


