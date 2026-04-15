# Multi Controller Master/Slave

## Wat is het?
**Multi Controller** is een nieuwe functie binnen de **Solar Controller** waarmee één ESP als **master** meerdere uitgangen kan aansturen:

- **lokaal** op dezelfde ESP via extra PWM-uitgangen
- **via het netwerk** door één of twee andere Solar Controllers als **slave** te laten meelopen

De code in deze versie ondersteunt maximaal **3 controllers/slots totaal**.
Daarbij blijft **controller 1 altijd de vaste lokale uitgang op GPIO25**.

---

## Hoe werkt de master/slave-regeling?
In een master/slave-opstelling bepaalt de **master** de regeling voor de hele groep.
De master plant:

- welke controllers actief mogen zijn
- de volgorde van opregelen en terugregelen
- vanaf welk PWM-percentage de volgende controller mag meedoen
- welk scenario actief is, zoals zonne-overschot, goedkope prijs, negatieve prijs of forceren

De **slave** denkt in netwerkmodus niet zelfstandig mee over overschot of prioriteit.
De slave ontvangt de doelen van de master en voert die lokaal uit.

Kort gezegd:

- **master bepaalt de strategie**
- **slave voert uit**
- **lokale veiligheid blijft altijd actief**

---

## Lokale en netwerkbesturing
De master kan in deze versie beide vormen combineren:

### Lokale besturing
De master stuurt PWM-uitgangen aan op dezelfde ESP.
Dat is de snelste vorm van aansturing.

- controller 1 = altijd lokaal op **GPIO25**
- extra lokale PWM-uitgangen kunnen op **GPIO26, GPIO27 of GPIO32**

### Netwerkbesturing
De master kan ook een andere Solar Controller aansturen via het netwerk.
Die andere ESP werkt dan als slave.

Belangrijk in deze code:

- netwerk-controllers mogen alleen op de **laatste actieve posities** staan
- dus bijvoorbeeld **lokaal / lokaal / netwerk** of **lokaal / netwerk / netwerk**
- **lokaal / netwerk / lokaal** is niet toegestaan

---

## Gedrag van de regeling
Bij **zonne-overschot** werkt de groepsregeling sequentieel:

1. eerst de controller met de hoogste voorrang
2. daarna de volgende vanaf het ingestelde startpercentage
3. daarna eventueel de derde

Bij **forceren of prioriteitswarmte** werkt de regeling per actief slot, maar altijd binnen de veiligheidsgrenzen.

---

## Veiligheid
Ook in master/slave blijft veiligheid leidend.
De code houdt daarom rekening met bestaande begrenzingen zoals:

- fasebeveiliging
- loadbalancing
- min/max PWM per controller
- soft-start
- lokale safety-clamping op de slave

Als een netwerkverbinding of configuratie niet bruikbaar is, valt de regeling veilig terug.

---

## Samengevat
De nieuwe **Multi Controller Master/Slave** functie maakt van meerdere Solar Controllers één gecoördineerd systeem.

De **master** regelt de groep.
De **slave** volgt.
Lokale PWM-uitgangen en netwerk-controllers kunnen gecombineerd worden.
