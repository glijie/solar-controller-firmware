# Multi Controller Master/Slave instellen

## 1. Open de tab Multi Controller
Ga in de Solar Controller naar **Settings** en open de sectie **Multi Controller**.

---

## 2. Zet Multi Controller aan
Zet **Multi Controller actief** op **Aan**.

- **Uit** = de Solar Controller werkt lokaal als standalone
- **Aan** = de controller werkt in **Master** of **Slave** modus

Daarna kies je bij **Werking zodra Multi Controller aan staat**:

- **Master**
- **Slave**

---

## 3. Instellen als Master
Kies **Master** als deze ESP de groep moet aansturen. ---> klik eerst op opslaan

### Algemene instellingen
Vul in:

- **Aantal controllers in groep**: 1 t/m 3
- **Eigen controller-ID**
- **Groepsnaam**

### Controller 1
Controller 1 is altijd:

- **lokaal**
- vaste PWM-uitgang op **GPIO25**

Dit hoeft dus niet gekozen te worden.

### Controller 2 en 3
Voor controller 2 en 3 kies je per slot:

- **Type**
  - Lokale PWM-uitgang
  - Netwerk-controller (slave ESP)
- **Naam**
- **Peer IP-adres** bij netwerk-controller
- **Lokale PWM-pin** bij lokaal slot
  - GPIO26
  - GPIO27
  - GPIO32
- **Volgende mee vanaf PWM %**
- **Volgorde opregelen**
- **Volgorde terugregelen**
- **Min PWM %**
- **Max PWM %**
- **Meedoen bij zonne-overschot**
- **Meedoen bij goedkope prijs**
- **Meedoen bij negatieve prijs**
- **Forceren / prioriteitswarmte toestaan**

### Belangrijke regel
Netwerk-controllers mogen alleen op de **laatste actieve positie(s)** staan.

Goed:

- lokaal / lokaal / netwerk
- lokaal / netwerk / netwerk

Niet toegestaan:

- lokaal / netwerk / lokaal

Sla daarna de instellingen op.

---

## 4. Instellen als Slave
Kies **Slave** als deze ESP opdrachten van een master moet volgen. ----> klik eerst op opslaan.

In slave-modus werkt deze code bewust eenvoudiger:

- **controller 1** blijft de lokale uitgang op **GPIO25**
- **controller 2** is de vaste **master-koppeling**
- **controller 3** wordt niet gebruikt
- **Aantal controllers in groep** staat vast op **2**
- **Eigen controller-ID** staat vast op **1**

### Wat je invult
Bij de master-koppeling vul je in:

- **Naam**
- **Master IP-adres**

Daarnaast kun je instellen:

- **Lokale legionella mag master overrulen**

Die optie zorgt ervoor dat de slave tijdelijk zijn eigen lokale legionella-programma mag draaien. Buiten legionella blijft de master leidend.

Sla daarna de instellingen op.

---

## 5. Controle na het opslaan
### Op de master zie je onder andere:

- hoeveel netwerk-slots zijn ingesteld
- hoeveel peers online zijn
- of writeback/TCP goed werkt
- welke deelnemers in het groepsmodel zitten

### Op de slave zie je onder andere:

- of de TCP-server luistert
- of de master-link online is
- of remote target-control actief is
- de leeftijd van de laatste TCP/target-update
- de reden van eventuele veilige terugval

---

## 6. Praktisch advies
### Gebruik Master als:

- deze ESP de hoofdregeling moet doen
- je lokale PWM-uitgangen wilt combineren met netwerk-slaves

### Gebruik Slave als:

- deze ESP alleen een extra uitvoerende controller is
- de regeling volledig door de master bepaald moet worden

---

## Samengevat
- **Master** stuurt aan
- **Slave** voert uit
- **Controller 1** blijft altijd lokaal op **GPIO25**
- extra lokale PWM alleen op **GPIO26, GPIO27 of GPIO32**
- maximaal **3 controllers totaal**
- netwerk-slots alleen aan het einde van de actieve volgorde
