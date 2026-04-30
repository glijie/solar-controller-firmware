# Solar Controller Homey App v0.7.73

Release notes voor de Homey app update voor Solar Controller firmware `v2026.04.29.01 step300`.

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

## Installatie

```bash
npm install
homey login
homey app install
```

## Release ZIP

```text
Solar_Controller_Homey_v0.7.73_multi_esp.zip
```

