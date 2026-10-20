# Solar Controller

Solar Controller is firmware voor een ESP32 die elektrische verwarming, bijvoorbeeld een boiler, kan aansturen op basis van beschikbaar zonne-overschot. Je kunt de regeling ook laten reageren op temperatuur en elektriciteitsprijzen. Voor het schakelen of regelen van vermogen is passende externe hardware nodig.

## Wat kun je ermee?

- Het actuele netvermogen uitlezen via een ondersteunde meterbron, waaronder Youless, HomeWizard en Smart Gateways, en daarop de PWM-uitgang voor een externe vermogensregelaar aanpassen.
- Een boiler of verwarmingsinstallatie aansturen met temperatuurgrenzen, fasebewaking en optionele programma's voor geforceerd verwarmen, prijssturing en Legionella.
- Meerdere controllers samen laten werken in een master/slave-opstelling.
- De werking volgen en instellingen aanpassen via de webinterface, Home Assistant (MQTT) en Homey/REST.
- Nieuwe firmware installeren via een `.bin`-bestand of via een GitHub-release.

Welke functies je gebruikt en hoe je de uitgangen aansluit, hangt af van je eigen hardware en instellingen.

## Broncode en updates

Dit pakket bevat de ESP32-broncode. De sketch staat in `firmware/Solar_Controller/`. Houd de naam van de map `Solar_Controller` gelijk aan het hoofd-`.ino`-bestand `Solar_Controller.ino`. Voor zelf compileren heb je de Arduino ESP32-omgeving en de bibliotheken ArduinoJson, PubSubClient, OneWire en DallasTemperature nodig.

De ingebouwde online firmware-update kijkt naar de nieuwste release van `glijie/solar-controller-firmware` en verwacht daarin `version.json` en `SolarController.bin`. Alleen de broncode op GitHub plaatsen maakt dus nog geen nieuwe firmware beschikbaar voor de updatefunctie.

## Online apparaatentelling

De ESP32 meldt zich na het opstarten en daarna ongeveer eenmaal per 24 uur bij een Cloudflare Worker. De melding bevat het van het MAC-adres afgeleide serienummer en de firmwareversie. Hierbij worden geen wifi- of MQTT-wachtwoorden, meetwaarden of instellingen verstuurd. De code waarmee apparaten zich melden staat in de openbare firmware; het uitlezen van de apparatenlijst vereist een aparte privésleutel. De telling is indicatief: apparaten zonder melding ontbreken en valse meldingen zijn mogelijk.

## Licentie

De code mag gratis privé en voor andere niet-commerciële doeleinden worden gebruikt, aangepast en gedeeld volgens de [licentie in Nederlands en Engels](LICENSE.md). Commercieel gebruik, waaronder verkoop en betaalde installatie, vereist vooraf schriftelijke toestemming van de rechthebbende.

Wijzigingen per firmwareversie staan in [CHANGELOG.md](CHANGELOG.md).
