# Solar Controller – v2026.11.06.01

## Aangepast in v2026.11.06.01

- Home Assistant MQTT Discovery: vijf ongeldige stapgroottes gecorrigeerd (issue #34).
- Legionella-temperatuur krijgt bij ontbrekende meting de status *Onbekend* zonder HA-logfout.
- Handmatig start- en eindtijdvenster kan nu via Home Assistant met `HH:MM` worden ingesteld; bestaande MQTT-minutencommando's werken ook nog.
- Home Assistant-instelbereiken afgestemd voor zonblokken, energiebelasting, inkoopkosten, BTW en boost-drempel. Het huidige maximum van 10.000 W in Home Assistant blijft bewust gehandhaafd.
- Geen wijzigingen aan bestaande entiteits-ID's, MQTT-topics, REST/Homey-API, wifi/MQTT-herverbinding of DS18B20-publicatie.

## Aangepast in v2026.10.24.01

- **Aantal dagdelen** toegevoegd aan de goedkoopste ELEC-blokkenregeling: `1` of `2`.
- `1` is standaard en behoudt exact de bestaande planning.
- Bij `2` verschijnen twee instelbare tijdgrenzen, standaard **07:00** en **19:00**.
- Standaard worden daarmee onafhankelijk **19:00–07:00** en **07:00–19:00** geoptimaliseerd.
- Ieder dagdeel gebruikt zelfstandig maximaal de ingestelde **N** aaneengesloten blokken waarin ELEC goedkoper is dan GAS.
- Het nachtvenster kan over middernacht heen plannen met morgenprijzen wanneer die beschikbaar zijn.
- Overige regeling is niet gewijzigd.

## Aangepast in step317

De slimme ELEC-blokkenregeling heeft nu een eigen temperatuurkeuze. Met **Gebruik boiler-doeltemperatuur bij ELEC-blokken** bepaalt de gebruiker zelf of de ingestelde boiler-doeltemperatuur de AUTO_ELEC-run mag stoppen.

- Gebruikt uitsluitend de expliciet gekozen boilersensor uit de Legionella/boiler-instellingen.
- Geen boilersensor gekozen of sensor ongeldig: alleen prijsplanning is leidend.
- Toggle AAN: stoppen bij de ingestelde boiler-doeltemperatuur.
- Toggle UIT: het gekozen N-venster wordt niet door de doeltemperatuur afgebroken.
- Na opnieuw afkoelen tot 5 °C onder de doeltemperatuur kan een nieuwe warmtevraag een nieuw N-plan openen.
- Staat volledig los van **Planning** en **Temperatuur-modus gebruiken** van Legionella.
- Instelling is beschikbaar via webinterface, Home Assistant/MQTT en REST/Homey.

## Aangepast in step316

- Integratielaag voor **Gas vs Elektra / AUTO_ELEC** compleet gemaakt.
- Home Assistant krijgt nu ook schakelaars voor **Advies Gas vs Elektra** en **Voer ELEC-advies uit**.
- Home Assistant krijgt een sensor **Verwarmadvies** en een binaire sensor **Verwarmen via ELEC-advies actief**.
- MQTT command/state topics toegevoegd voor `heat/auto` en `heat/exec_el`.
- `/api/config` kan nu naast `heat_ch_en` en `heat_ch_n` ook `heat_auto`, `heat_exec_el` en de Gas-vs-Elektra-parameters lezen/schrijven.
- `/api/config` uitgebreid met fasebewaking, prioriteits-soft-start en Legionella-relaisautomatisering voor Homey/REST-integraties.
- `/api/status_light` geeft nu compact de actuele AUTO_ELEC-status en het gekozen goedkope-blokkenplan terug.
- Wijzigingen in Gas-vs-Elektra-instellingen maken het bestaande goedkope-blokkenplan ongeldig, zodat opnieuw met de actuele instellingen wordt gepland.

Uitbreiding op step315. De bevestigde golden baseline blijft step309 totdat een nieuwere versie in de praktijk is bevestigd.

## Aangepast in step315

- De instellingen **Alleen verwarmen tijdens goedkoopste aaneengesloten ELEC-blokken** en **Max. aantal goedkope aaneengesloten blokken** zijn nu extern aanstuurbaar.
- Home Assistant krijgt via MQTT Discovery automatisch een schakelaar en een nummer-entiteit.
- MQTT commando/state topics toegevoegd onder `heat/cheap_blocks/...`.
- `/api/config` leest en schrijft nu `heat_ch_en` en `heat_ch_n`, zodat Homey Flow-kaarten deze instellingen via REST kunnen aanpassen.
- Elke externe wijziging maakt het huidige goedkope-blokkenplan ongeldig, zodat direct met de nieuwe instelling opnieuw wordt gepland.


Uitbreiding op step313. De bevestigde golden baseline blijft step309 totdat een nieuwere versie in de praktijk is bevestigd.

## Aangepast in step314

- Bij het maken van een nieuw slim AUTO_ELEC-plan kijkt de controller nu vanaf **NU tot 24:00 naar alle resterende prijsblokken van vandaag**.
- Alleen blokken waarin ELEC volgens de bestaande Gas-vs-Elektra-berekening goedkoper is mogen in het gekozen venster zitten.
- De gekozen blokken blijven altijd **aaneengesloten**; een GAS-blok wordt nooit gebruikt om N vol te maken.
- Bestaat ergens vandaag een geschikte reeks van N blokken, dan wordt van alle zulke reeksen de goedkoopste gekozen.
- Bestaat nergens een volledige reeks van N, dan wordt de **langste beschikbare geschikte reeks onder N** gebruikt en daarvan de goedkoopste variant.
- Dit geldt voor iedere ingestelde N-waarde, dus niet alleen voor N=1.
- Eerste activering, nieuwe dag, een nieuwe ELEC-periode na GAS en een nieuwe temperatuur-warmtevraag maken op deze manier opnieuw een plan vanaf dat moment tot 24:00.
- Daardoor kan bijvoorbeeld bij N=1 bewust worden gewacht op een goedkoper ELEC-blok later op dezelfde dag in plaats van automatisch het huidige ELEC-blok te nemen.

## Temperatuurgedrag uit step313 behouden

- **Temperatuur-modus gebruiken AAN**: stoptemperatuur mag AUTO_ELEC stoppen; op/onder starttemperatuur ontstaat opnieuw warmtevraag.
- **Temperatuur-modus gebruiken UIT + sensor toegewezen**: stoptemperatuur wordt genegeerd en de gekozen N-run wordt afgemaakt. De sensor kan wel een nieuwe warmtevraag openen wanneer de boiler later weer op/onder de starttemperatuur komt.
- Zonder toegewezen/geldige sensor blijft de regeling prijs-/periodegestuurd.

## Overig behouden

- Om 00:00: nieuw plan / nieuwe kansen.
- GAS wordt actueel goedkoper: AUTO_ELEC stopt direct en het plan vervalt.
- Wordt ELEC later dezelfde dag opnieuw goedkoper: nieuw plan vanaf dat moment tot 24:00.
- **Voer ELEC-advies uit** blijft de hoofdschakelaar; **Alleen verwarmen tijdens goedkoopste aaneengesloten ELEC-blokken** is de extra beperking.
- Staat de goedkoopste-blokken-toggle uit, dan blijft de bestaande step309 AUTO_ELEC-werking ongewijzigd.

## Release-structuur

De Arduino-sketchmap en het hoofd-`.ino`-bestand hebben exact dezelfde naam:

`Solar_Controller_v2026_10_24_01`
