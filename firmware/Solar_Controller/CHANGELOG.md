# Wijzigingen per firmwareversie

## v2026.09.19.02

- Eigen toggle toegevoegd: **Gebruik boiler-doeltemperatuur bij ELEC-blokken**.
- Slimme N-blokkenregeling gebruikt nu de expliciet gekozen boilersensor (`legTempAddrStr`) en niet langer de controller-temperatuurvrijgave.
- Geen vaste sensornaam of sensornummer: het opgeslagen DS18B20-adres is leidend.
- Geen sensor gekozen / sensor ongeldig: temperatuur speelt geen rol en prijsplanning blijft werken.
- Toggle AAN: AUTO_ELEC stopt bij de ingestelde boiler-doeltemperatuur.
- Toggle UIT: doeltemperatuur stopt de N-run niet.
- Nieuwe warmtevraag kan opnieuw worden herkend bij afkoelen tot doeltemperatuur minus 5 °C.
- Legionella **Planning** en **Temperatuur-modus gebruiken** beïnvloeden deze nieuwe toggle niet. Alleen een daadwerkelijk actieve Legionella-run houdt zijn bestaande prioriteit.
- Nieuwe toggle toegevoegd aan Home Assistant/MQTT en REST/Homey (`heat_ch_bt`).
- Statusdiagnose uitgebreid met gekozen sensor, actuele temperatuur, doeltemperatuur en herstartgrens.

## v2026.09.19.01

- Home Assistant MQTT Discovery uitgebreid met bestuurbare schakelaars voor **Advies Gas vs Elektra** (`heat_auto`) en **Voer ELEC-advies uit** (`heat_exec_el`).
- Home Assistant toont nu ook **Verwarmadvies** en **Verwarmen via ELEC-advies actief**.
- MQTT retained state/command topics toegevoegd: `heat/auto/state|set` en `heat/exec_el/state|set`.
- `/api/config` uitgebreid met `heat_auto`, `heat_exec_el`, `heat_kwhm3`, `heat_geff`, `heat_eeff`, `heat_hyst`, `heat_minsw`.
- `/api/config` uitgebreid met fasebewaking (`pg_*`), prioriteits-soft-start (`ss_*`) en Legionella-relaisautomatisering (`leg_rel_*`) voor REST/Homey.
- `/api/status_light` uitgebreid met `heat_exec_active`, slimme ELEC-blokkenconfig en compact plan/start/einde/interval.
- Externe wijzigingen die de Gas-vs-Elektra-beslissing of slimme planning beïnvloeden invalidateren het lopende plan en laten de planner opnieuw rekenen.

## v2026.09.18.06

- Home Assistant MQTT Discovery: nieuwe schakelaar voor `Alleen goedkoopste ELEC-blokken`.
- Home Assistant MQTT Discovery: nieuw instelbaar getal `Max. goedkoopste aaneengesloten ELEC-blokken` (1..96).
- MQTT state/set topics toegevoegd voor beide instellingen.
- `/api/config` uitgebreid met `heat_ch_en` en `heat_ch_n` voor Homey/REST-bediening.
- Externe wijzigingen resetten direct het huidige slimme ELEC-plan zodat opnieuw wordt gepland.
- Bestaande v2026.09.18.05-regellogica is verder ongewijzigd.

## v2026.09.18.05

- Nieuw slim AUTO_ELEC-plan kijkt vanaf NU tot 24:00 naar alle resterende blokken van vandaag.
- Alle toekomstige blokken met geldig ELEC-advies kunnen meedoen; GAS-blokken breken een aaneengesloten venster en worden nooit toegevoegd.
- Als een volledige reeks van N bestaat, wordt de goedkoopste aaneengesloten reeks van precies N gekozen.
- Als N nergens volledig haalbaar is, wordt de langste geschikte reeks onder N gekozen; bij meerdere even lange reeksen wint de goedkoopste.
- Geldt voor iedere N-waarde. Eerste activering kan daardoor bewust wachten op een goedkoper venster later op dezelfde dag.
- Nieuwe dag, GAS -> later ELEC en nieuwe temperatuur-warmtevraag openen opnieuw een plan vanaf dat moment tot 24:00.
- Temperatuurgedrag uit v2026.09.18.04 blijft behouden: stoptemperatuur begrenst alleen wanneer Temperatuur-modus gebruiken AAN staat.
- Helptekst bij de instellingen gecorrigeerd zodat het temperatuurgedrag overeenkomt met de firmware.

## v2026.09.18.04

- Temperatuurmodus AAN: bestaande start-/stop-hysterese blijft leidend voor slimme AUTO_ELEC.
- Temperatuurmodus UIT + sensor toegewezen: stoptemperatuur onderbreekt de N-run niet meer; de gekozen goedkope ELEC-blokken worden afgemaakt.
- Bij temperatuurmodus UIT blijft de sensor wel actief als detector voor nieuwe warmtevraag: na eerst boven starttemperatuur te zijn geweest, geeft een latere daling tot/op starttemperatuur opnieuw recht op een nieuw N-plan bij ELEC-advies.
- Zonder sensor blijft de bestaande prijs-/periodeherstart van v2026.09.18.03 gelden.
- `/api/heat_compare` uitgebreid met `cheap_temp_mode_enabled`.
- Overige v2026.09.18.03-planning ongewijzigd.

## v2026.09.18.03

- Slimme goedkoopste-ELEC-planning beperkt tot de resterende blokken van vandaag; morgen kan vandaag niet meer uitstellen.
- Om 00:00 wordt altijd opnieuw gepland.
- Als `N` groter is dan de resterende aaneengesloten ELEC-periode van vandaag, worden alle resterende geschikte blokken gebruikt en kan het huidige blok direct starten.
- GAS goedkoper stopt AUTO_ELEC direct; een latere nieuwe ELEC-periode op dezelfde dag krijgt opnieuw een vers N-plan.
- Een aan controller 1 toegewezen DS18B20 wordt voor deze functie gebruikt, ook wanneer de normale temperatuurvrijgave uit staat.
- Bestaande start-/stoptemperatuur wordt als hysterese gebruikt: stoptemperatuur bereikt = warmtevraag klaar; later weer op/onder starttemperatuur = nieuwe warmtevraag en nieuw N-plan bij ELEC-advies.
- Zonder toegewezen/geldige sensor blijft de prijsplanning leidend.
- `/api/heat_compare` uitgebreid met diagnosevelden voor sensor, temperatuurgrenzen en warmtevraag.
- v2026.09.18.02 compile-fix voor `EnergyApi.h` blijft behouden.

## v2026.09.18.02

- Compilefout in `EnergyApi.h` opgelost: `Arduino.h` wordt nu ingeladen voordat `String` in de functie-declaratie wordt gebruikt.
- Functionaliteit van de nieuwe goedkoopste aaneengesloten ELEC-blokken blijft verder gelijk aan v2026.09.18.01.

## v2026.09.18.01

- Gas vs Elektra uitgebreid met een optionele uitvoering op de goedkoopste aaneengesloten ELEC-blokken.
- Nieuw instelbaar maximum `N`; blokduur volgt automatisch de bestaande uur-/kwartierprijsinstelling.
- Is een ELEC-periode korter dan `N`, dan worden alleen de beschikbare opeenvolgende blokken gebruikt.
- Een GAS-blok wordt nooit toegevoegd om `N` vol te maken.
- Bij langere ELEC-periodes wordt het goedkoopste aaneengesloten venster van maximaal `N` blokken gekozen.
- Wordt GAS tijdens heating goedkoper, dan stopt AUTO_ELEC direct; bij een nieuwe ELEC-periode wordt opnieuw gepland.
- Goedkope nachtelijke blokken worden op dezelfde manier meegenomen.
- Nieuwe functie staat standaard UIT, zodat de bestaande v2026.09.07.01-werking ongewijzigd blijft als deze niet wordt ingeschakeld.

## v2026.09.07.01

- Bestaande ESPmDNS-laag uitgebreid met `_solarcontroller._tcp` op poort 80 voor Homey ManagerDiscovery.
- Bestaand Solar Controller serienummer (`SC-` + volledige 48-bit ESP32 base MAC) wordt hergebruikt als stabiele discovery-ID; geen tweede ID-systeem toegevoegd.
- mDNS TXT-records toegevoegd: `id`, `model=solar-controller`, `fw`, `name` en `api=1`.
- Een lege apparaatnaam schakelt mDNS niet meer uit: er wordt automatisch een korte fallback-hostname `sc-XXXXXXXXXXXX` gebruikt.
- Een handmatig ingestelde apparaatnaam en de bestaande `http://<naam>.local` toegang blijven ongewijzigd werken.
- Na WiFi `GOT_IP` wordt mDNS vanuit de hoofd-loop opnieuw gepubliceerd, zodat discovery na reconnect of een DHCP-adreswijziging terugkomt.
- Geen functionele wijzigingen aan PWM, P1/meters, Legionella, relais, temperatuurregeling, prijslogica, OTA, Multi Controller, MQTT, Home Assistant, REST/API of collector.

## v2026.08.16.01

- WiFi-diagnose toegevoegd, uitsluitend via `/api/system`.
- Runtime disconnect/reconnect tellers in RAM; compacte `wifi_previous_session` diagnose in NVS.
- WiFi power-save in RUN/STA permanent `WIFI_PS_NONE`; Multi Controller zet dit niet meer terug naar `WIFI_PS_MIN_MODEM`.
- Niet-blokkerende runtime `WiFi.reconnect()` met 5s eerste wachttijd en daarna 15/30/60/120s backoff.
- Config-AP gedrag, watchdog, UI, MQTT en overige controllerlogica zijn bewust niet aangepast.

## v2026.08.15.01

- Legionella-pagina overzichtelijker ingedeeld: relaisinstellingen staan nu in een eigen kaart `Relais`.
- Planning, temperatuur-modus, sensorinstellingen en Legionella-status blijven onder `Planning`.
- Geen functionele wijziging aan de Legionella-run, GPIO18-relaislogica, startvertraging, nalooptijd of handmatige bediening.

## v2026.08.11.01

- Correctie: handmatige relaisacties tijdens een Legionella-run annuleren de automatische relais-eindactie niet meer.
- Als `Relais automatisch inschakelen` aan staat, blijft handmatig AAN/UIT via Home/API/MQTT/Home Assistant tussendoor mogelijk.
- De ingestelde automatische inschakeling blijft echter actief en het relais wordt bij het einde van de Legionella-run na de ingestelde nalooptijd altijd UIT geschakeld.
- Een handmatige wijziging tijdens de inschakelvertraging annuleert het geplande automatische AAN-moment niet meer.
- Start- en nalooptijd blijven ongewijzigd instelbaar van 0 t/m 60 minuten; overige Solar Controller-logica is niet aangepast.

## v2026.08.10.02

- Instelbare inschakelvertraging voor het automatische Legionella-relais toegevoegd: 0 t/m 60 minuten.
- Bij 0 minuten schakelt GPIO18 direct in zoals in v2026.08.10.01.
- Een handmatige relaisactie via Home/API/MQTT/Home Assistant tijdens de wachttijd annuleert de automatische inschakeling voor die run.
- Stopt de Legionella-run vóór het verstrijken van de inschakelvertraging, dan wordt het relais niet meer ingeschakeld en start er geen nalooptijd.
- Bestaande nalooptijd, handmatige override en overige Solar Controller-logica zijn niet gewijzigd.

## v2026.08.10.01

- Bestaande relaisuitgang GPIO18 kan optioneel automatisch inschakelen zodra een Legionella-run start.
- Nieuwe Legionella-instelling `Relais automatisch inschakelen`; standaard UIT zodat bestaande installaties ongewijzigd blijven.
- Instelbare relais-nalooptijd van 0 t/m 60 minuten na het stoppen van de Legionella-run.
- Handmatige relaisbediening via dashboard, API/MQTT en Home Assistant blijft altijd mogelijk en heeft voorrang op de automatische Legionella-aansturing.
- Bestaande Legionella-verwarming, relais-hardwareconfiguratie en overige regeling zijn niet gewijzigd.

## v2026.07.27.01

- GitHub issue #13 opgelost: ontbrekende gas- of elektraprijzen worden niet meer behandeld alsof ELEC goedkoper is.
- Nieuwe geldigheidsstatus voor de gas/elektra-vergelijking: `gas`, `elec` of `unknown`.
- `Voer ELEC-advies uit` activeert maximaal verwarmen alleen bij een geldige actuele vergelijking én werkelijk goedkoper elektrisch verwarmen.
- Bij ontbrekende of verouderde prijsdata blijft AUTO_ELEC uit en gaat de normale zonne-overschotregeling verder.
- Gasprijs-download probeert na een mislukking automatisch iedere 60 seconden opnieuw; na een geslaagde download geldt weer het normale interval van 3 uur.
- Herstel start direct opnieuw zodra wifi en geldige tijd terug beschikbaar zijn.
- Voor automatische regeling wordt alleen de actuele gasdag gebruikt; de bestaande fallback blijft uitsluitend voor grafiek/weergave behouden.
- Multi Controller gebruikt nu dezelfde centrale fail-safe AUTO_ELEC-controle en kan de beveiliging niet meer omzeilen.
- Dashboard toont `ONBEKEND` en `-` zolang een volledige prijsvergelijking niet mogelijk is.
- Geen functionele wijziging aan Youless, HomeWizard, Smart Gateways, Force Heat, Legionella, Price Boost of fasebewaking.

## v2026.05.30.01

- Smart Gateways `gateway_model` wordt nu gelezen in de bestaande Smart Gateways reader.
- Voor `gateway_model: DSMR-BELGIUM` wordt `PowerDeliveredNetto` omgerekend van kW naar watt.
- Voor de bestaande werkende NL/Connectix Smart Gateways variant blijft `PowerDeliveredNetto` ongewijzigd zoals in v2026.05.10.01.
- Geen wijzigingen aan Youless, HomeWizard, fasebeveiliging, PWM-regeling, Force Heat, Legionella of UI-logica.

## v2026.05.10.01

- Settings > MQTT heeft nu een knop `HA Discovery opschonen`.
- De knop meldt alle bekende retained Home Assistant MQTT Discovery-configs van deze controller af en meldt daarna alleen de actuele geldige entiteiten opnieuw aan.
- Oude Multi Controller slot-entiteiten, inclusief legacy slot 4 t/m 8, worden meegenomen in de cleanup.
- Bij het uitzetten van `MQTT inschakelen` of `MQTT Discovery` meldt de controller HA Discovery eerst netjes af voordat de instellingen worden opgeslagen en de controller reboot.
- Chip-id gebaseerde HA-identiteit en retained discovery blijven verder ongewijzigd.

## v2026.04.29.01

- Energy strategy: vierde schema-optie `Uit (zonneregeling uit)` toegevoegd.
- `Uit` blokkeert alleen NORMAL/zonne-overschot; Force heat, Legionella, Prijs/Boost en ELEC-advies blijven werken.
- MQTT/Home Assistant publiceert en accepteert nu ook `UIT` voor `sun_sched_label` / `sun_s_mode=3`.
- Dashboard toont apart `Zonneregeling uit` in plaats van `Wacht op tijdvenster`.

## v2026.04.27.01

- Dashboard toont gebruikersvriendelijke labels voor enkele live-modussen zoals `NORMAL_BLOCKED`, zonder de onderliggende API/MQTT/Home Assistant/Homey waarden te wijzigen.
- `RUN`, `CONFIG`, `IDLE`, `RUNNING`, `STEP` en de bestaande status-contracten blijven ongewijzigd.
- Dashboardtekst `Overschot` hernoemd naar `Regeloverschot` om duidelijk te maken dat dit de regelwaarde is, niet puur de actuele teruglevering.

## v2026.04.26.06

- Settings-save publiceert na opslaan nu alle relevante retained MQTT state topics opnieuw.
- Home Assistant switches/numbers/selects blijven daardoor direct synchroon wanneer waarden via de Solar Controller Settings worden aangepast.
- Geen wijzigingen aan MQTT topicnamen, payloads, API-contracten of regeling.

## v2026.04.26.05

- Oude lege `moved to ...` commentblokken en no-op sectiekoppen uit de hoofd-INO verwijderd.
- Geen functionele wijzigingen aan API, Home Assistant discovery, MQTT topics/payloads of regeling.

## v2026.04.26.04

- MQTT tuning setters verplaatst uit de hoofd-INO naar `src/MqttTuning.cpp`.
- Hoofd-INO verder verkleind zonder wijzigingen aan tuning topics, payloads of API-contracten.

## v2026.04.26.03

- MQTT retained state publish-code verplaatst uit de hoofd-INO naar `src/MqttPublish.cpp`.
- Home Assistant MQTT Discovery verplaatst uit de hoofd-INO naar `src/HomeAssistantDiscovery.cpp`.
- Hoofd-INO verder verkleind zonder bewuste wijzigingen aan bestaande MQTT topics, HA discovery velden of API-contracten.

## v2026.04.26.02

- Compile-fix: `sysPublishEnabled`, `sysPublishIntervalSec` en `statusPublishIntervalSec` gedeeld via `app_globals.h`, zodat `SettingsRoutes.cpp` na de module-split kan compileren.
- Firmwareversie verhoogd naar `v2026.04.26.02`.

## v2026.04.26.01

- Hoofd-INO verkleind door dashboard, settings, control/legionella, energy API, webhelpers, statuslabels en MQTT-parsers naar `src` modules te verplaatsen.
- Geen bewuste wijzigingen aan bestaande API-routes, Home Assistant discovery-velden of MQTT topic/payload-contracten.
- Firmwareversie bijgewerkt zodat UI/API deze refactor-release als nieuwe firmware toont.

## v2026.04.25.02

- Gasgrafiek teruggezet naar de oude schaal/weergave.
- Alleen stroomprijsgrafieken Vandaag/Morgen gebruiken de nieuwe 0-lijn: negatieve prijzen groen onder 0, positieve prijzen blauw boven 0.
- Geen wijziging aan prijsdata, gasdata, regeling, API, MQTT of overige UI.

## v2026.04.25.01

- Prijsgrafiek tekent nu rond de 0-lijn: positieve prijzen boven de lijn blauw, negatieve prijzen onder de lijn groen.
- Alleen grafiekweergave aangepast; prijsdata, regeling, API, MQTT en overige UI ongewijzigd gelaten.

## v2026.04.23.03

- MQTT `<base>/status` publiceert nu ook `leg_start_hour`, `leg_start_minute` en `leg_output_limit_pct` zodat Home Assistant JSON-template sensoren geen warnings meer geven.

## v2026.04.23.02

- MQTT `<base>/status` publiceert nu ook `leg_schedule_enabled`, `leg_dow`, `leg_duration_min`, `leg_target_temp_c` en `leg_hold_min`
- Home Assistant MQTT discovery-templates voor legionella matchen hiermee weer met de werkelijk gepubliceerde JSON status
- minimale fix: alleen status JSON/version/changelog aangepast; overige regeling ongemoeid gelaten

## v2026.04.23.01

- MQTT state `sun_sched_label` voor Home Assistant select nu gelijkgetrokken met de discovery-opties: `ALWAYS`, `HANDMATIG`, `CHEAPEST DAYLIGHT`
- logspam opgelost waarbij HA `CHEAPEST_DAYLIGHT` als ongeldige optie zag voor `select.solar_controller_solar_controller_zonregeling_schema_normal`
- minimale fix: alleen labelmapping/version/changelog aangepast; overige regeling ongemoeid gelaten

## v2026.04.17.07

- temperatuurvrijgave blokkeert nu alleen nog de normale regeling / zonne-overschot en niet meer force heat, legionella, goedkope prijs of negatieve prijs
- lokale runtime en master/slave groepsmodel negeren tempblokkering nu bewust in prio-scenario's; safety-limieten blijven wel leidend
- netwerkcontrollers kunnen in prio dus weer normaal meedoen, terwijl temperatuurvrijgave voor normale surplusverdeling actief blijft

- master toont in de Control-tab geen lokale temperatuursensor-keuze meer voor netwerkcontrollers/slaves
- temperatuurvrijgave voor netwerkcontrollers blijft lokaal op de slave beoordeeld en wordt op de master alleen informatief vermeld
- hidden/stale master-tempconfig voor netwerkcontrollers wordt genegeerd in runtime en summary, zodat de UI/logica weer klopt met het ontwerp

## v2026.04.17.05

- testmatrix toegevoegd voor standalone, master, slave en master+slave scenario
- documentatie aangevuld; code uit v2026.04.17.04 functioneel ongewijzigd

## v2026.04.17.04

- Control-tab uitgebreid met accordion voor temperatuurvrijgave per controller
- nieuwe save-handler `/control/save_temp_gate` toegevoegd
- status, sensorselectie, stop/starttemperatuur en vaste GPIO25-info zichtbaar in de UI

## v2026.04.17.03

- temperatuurvrijgave actief gekoppeld aan standalone PWM-aansturing
- lokale en remote controllers vallen nu uit de planner/verdeling wanneer temperatuurvrijgave blokkeert
- peer compact status uitgebreid met temp-vrijgave/beschikbaarheidsvelden voor master-slave

## v2026.04.17.02

- temperatuurvrijgave-config per controller toegevoegd met persistence in Preferences
- runtime/hysterese toegevoegd op basis van DS18B20-adressen
- nog geen actieve regeling/UI-integratie in deze stap

## v2026.04.17.01

- stap 1 van temperatuurvrijgave per controller uitgevoerd: functionele basis nu expliciet vastgelegd zonder bestaand regelgedrag te wijzigen
- nieuwe modulebasis `src/ControllerTemperatureGate.*` toegevoegd zodat config/runtime-logica straks netjes buiten de hoofd-INO en buiten de bestaande Multi Controller-bestanden verder kan groeien
- nieuwe ontwerpdocumentatie toegevoegd voor de functie `Temperatuurvrijgave controller`, inclusief bevestiging dat controller 1 lokaal altijd GPIO25 blijft en dat temperatuurvrijgave per controller werkt

## v2026.04.15.11

- afrondende projectdocumentatie toegevoegd voor de nieuwe TCP master/slave architectuur, inclusief hardwaretestmatrix en acceptatiecriteria
- README/STRUCTURE bijgewerkt zodat `MultiControllerTcpLink` zichtbaar onderdeel is van de firmware-opbouw
- geen extra logische codewijziging meer in deze stap; focus op overdraagbaarheid en gecontroleerd testen op echte hardware

- oude HTTP polling/writeback paden nu expliciet buiten gebruik gezet voor realtime master/slave control zodat ze niet per ongeluk nog mee gaan sturen
- beheer/status endpoints blijven bestaan, maar de code markeert nu duidelijk dat realtime control via TCP loopt
- dit ruimt de architectuur op zonder nuttige beheer-API's weg te gooien

- TCP-control writepad verder versneld: onnodige `flush()` verwijderd omdat `setNoDelay(true)` al actief is en flush juist extra blokkeren kan geven
- heartbeat/ack timing iets aangescherpt zodat status en verse master-targets sneller rondgaan zonder terug te vallen op zware polling
- deze stap pakt pure transportefficiëntie aan binnen de nieuwe TCP-architectuur

- UI en telemetrie uitgebreid met TCP-control info: realtime actief, poort, link-leeftijd, RTT en reconnect-tellers
- oude HTTP-gecentreerde statusregels in Multi Controller UI vervangen door TCP-control taal zodat diagnose nu aansluit op de nieuwe architectuur
- hiermee wordt direct zichtbaar waarom een slave wel of niet echt in de groep mee draait

- bij verlies van de TCP master-link wist de slave zijn remote targets nu direct en valt gecontroleerd terug in plaats van ze passief te laten verouderen
- reconnect dwingt direct een verse target-stroom af doordat de master zijn laatste target-cache per peer wist bij disconnect
- safety-contract verder verhard: link kwijt = geen oud master-commando meer blijven vasthouden

- slave laat zijn eigen lokale groepsvraag en planner nu niet meer meepraten tegen de master; netwerk-slave wacht alleen op master-targets of draait lokale override
- `mcRequestedGroupPercent` en plannerdoorloop gescheiden zodat follower-instellingen het mastergedrag niet meer kunnen beïnvloeden
- hiermee wordt het contract verder verhard: master bepaalt strategie, slave voert uit en clamp alleen lokaal op safety/override

- fail-safe/reconnect aangescherpt: master telt een slave niet meer als bruikbaar mee zodra de TCP-link wegvalt of te oud wordt
- wijziging van Multi Controller-instellingen reset nu ook direct de TCP-linkstatus zodat nieuwe IP/rol/slotconfig schoon herstart
- telemetrie/control-source bijgewerkt richting TCP zodat diagnose van master/slave gedrag duidelijker wordt

- coordinator schakelt netwerk-master/slave nu om naar de nieuwe persistente TCP-control link voor realtime besturing
- remote target TTL, servicetick en stale/offline timings aangescherpt zodat netwerk-slots sneller uit de groep vallen en sneller weer meekomen
- oude HTTP endpoints blijven bestaan voor beheer/status, maar niet meer als primair realtime-control pad

- nieuwe module `MultiControllerTcpLink` toegevoegd met een compact binair control/status frame voor master-slave verkeer
- persistente TCP-client/server basis voorbereid inclusief reconnect-tellers, RTT-meting, heartbeat-velden en slave statusopbouw
- nog niet leidend gemaakt in de coordinator; de volgende stap schakelt de realtime besturing over op deze nieuwe transportlaag

- stap 3 gestart: slave-contract verankerd als actuator + lokale safety clamp; ontwerpdocumentatie uitgebreid voor de TCP-richting
- voorbereidende modulebestanden toegevoegd voor de nieuwe persistente TCP-control link zonder de bestaande werkende standalone/lokale multi logica te raken
- nog geen functionele transportwissel; deze stap houdt de basis compile-veilig en maakt de volgende stap klein en gericht


- stap 1+2 van het nieuwe master/slave-traject uitgevoerd: de huidige v2026.04.13.01-netwerklaag diep vastgelegd en de definitieve voorkeursarchitectuur zwart-op-wit in de firmwaredocumentatie gezet
- nieuw document `docs/MULTI_CONTROLLER_MASTER_SLAVE_REDESIGN.md` toegevoegd met: huidige bottlenecks, wat behouden moet blijven, wat de slave nog wel/niet mag doen, en het doelmodel met persistente TCP control link + compacte snapshots
- huidige master/slave-codepaden expliciet beoordeeld op traagheid en instabiliteit: HTTP/JSON per actie, 2s poll/writeback, 900ms blocking timeout, 15s remote TTL en dubbele control-richting (master push + slave poll)
- geen functionele codewijziging in deze stap; standalone en lokale multi bewust ongemoeid gelaten zodat de goede basis bevroren blijft voor de volgende implementatiestappen

## v2026.04.13.01

- Correctie van versie-/datumregels naar 13 april 2026
- Geen inhoudelijke codewijziging ten opzichte van een eerdere versie
- status/telemetry opgeschoond rond het nieuwe leidende safety-principe: per slot en per groep worden nu `requested`, `allowed` en `assigned/applied` duidelijker gepubliceerd
- compacte peer-status levert nu ook `allowed_pct` en `mc_group_pwm_allowed_pct`, zodat master/groupmodel netwerkdeelnemers beter kan tonen zonder terug te vallen op ruwe requested targets
- group model gebruikt voor netwerkdeelnemers nu waar mogelijk de veilige `allowed`-target in plaats van blind de requested target
- bewust licht gehouden: geen extra meterreads, geen extra netwerkcalls en geen nieuwe plannerlaag; alleen status/telemetrie/cleanup voor consistentere debugging en UI

## v2026.04.12.20

- Multi Controller network cleanup: master phase guard is now a light remote writeback gate instead of a second aggressive scaler over all participants.
- In network master mode the master now only blocks extra remote ramp-up on a phase that is near its configured limit; active downward corrections stay local on each ESP.
- If master phase data is stale or invalid, remote targets are now frozen to the current assigned level instead of being forced to 0%.
- This reduces overlapping planner/master/slave phase-guard layers and keeps the fast local control path lighter.

## v2026.04.12.19

- Lokale Multi Controller-slots houden nu `requested / allowed / applied` beter gelijk door een centrale runtime-slottoepassing te gebruiken.
- Voor lokale multi-PWM wordt de zichtbare lokale target nu vastgehouden op de veilige toegestane waarde zodra fasebeveiliging of slotlimieten ingrijpen.
- Lokale groepsmodel-targets gebruiken nu de veilige `allowed` waarde, zodat lokale master-slots niet blijven tonen/plannen alsof er nog hoger wordt gevraagd terwijl de uitgang al is begrensd.

## v2026.04.12.18

- standalone prio / force / boost begrenst nu eerst het gevraagde target op een lichte `PhaseBudget`-allowed waarde vóór soft-start, zodat fase/loadbalancing ook in prioriteitsmodi leidend blijft
- prioriteits-soft-start gebruikt nu optioneel de laatst veilige toegepaste PWM als herankerpunt, zodat de ramp niet blijft doorlopen vanaf een oude interne startwaarde wanneer safety al heeft ingegrepen
- alleen het standalone prioriteitspad aangepast; Multi Controller planner/runtime en netwerk-master/slave zijn in deze stap bewust nog niet gewijzigd
- geen extra meter- of netwerkreads toegevoegd; de wijziging gebruikt alleen bestaande snapshotdata en lichte berekening

## v2026.04.12.17

- standalone zonne-overschot gebruikt nu al vóór de bestaande phase guard een lichte `PhaseBudget`-allowed cap, zodat de veilige fase-/loadbalancing-grens de PWM-opbouw eerder begrenst
- toegevoegd in `src/PhaseBudget.h/.cpp`: helper om uit actuele fase-stroom, geschatte eigen boilerstroom en ingestelde fasegrens een maximaal toegestane standalone PWM te berekenen
- `src/PwmOutput.cpp` begrenst dit allowed target alleen voor standalone `MC_SCENARIO_SURPLUS`; prio en Multi Controller zijn in deze stap bewust nog niet aangepast
- de wijziging gebruikt alleen de al ververste fase-snapshot uit v2026.04.12.16 en voegt geen extra meter- of netwerkreads toe, zodat de ESP snel blijft

## v2026.04.12.16

- basis gelegd voor de nieuwe fase/loadbalancing-fix met een lichte centrale `PhaseBudget` module (`src/PhaseBudget.h/.cpp`)
- de hoofd poll-loop ververst fasewaarden nu vóór de regelbeslissingen, zodat prio, zonne-overschot, standalone en Multi Controller in dezelfde poll met verse fase-info werken
- bestaande fase-status en `updatePhaseGuardDecision()` worden nu vanuit één centrale refresh bijgewerkt in plaats van pas achteraf aan het einde van de poll
- Smart Gateways vermijdt nu onnodige dubbele fase-reads vlak na een verse snapshot, zodat de ESP vlotter blijft
- bewust nog geen wijziging aan requested/allowed/applied logica; dit is stap 1 van de structurele fix

## v2026.04.12.15

- fix: in Multi Controller master-modus gebruiken lokale PWM-slots nu weer zachte opstart bij prioriteitsscenario's zoals Force Heat en Priority Heat
- opgelost in `src/MultiControllerRuntime.cpp` door per lokaal master-slot dezelfde soft-start-ramp toe te passen als al voor remote slave-targets werd gebruikt
- geen extra opschoonverwijderingen in deze stap; alleen gerichte bugfix in het actieve runtime-pad

## v2026.04.12.14

- Multi Controller toggle-uit + opslaan gefixt: in de multi-sectie betekent een niet-aangevinkte mc_en-checkbox nu echt "uit", zodat de controller na opslaan naar standalone lokaal terugvalt in plaats van direct weer terug te springen naar master/slave.

- Opschoonstap A uitgevoerd: duidelijk ongebruikte helperfuncties en wrappers verwijderd uit de hoofd-ino, Multi Controller-modules, MQTT helperlaag en relaymodule.
- Verwijderd zijn alleen functies/declaraties zonder actieve aanroep in de huidige codepaden, zoals oude prioriteits-soft-start/hard-cap wrappers, ongebruikte MQTT/peer wrappers en kleine resthelpers.
- Gedrag van standalone, Multi Controller, MQTT discovery en UI bewust ongemoeid gelaten; dit is een code-opruimstap zonder bedoelde functionele wijziging.

## v2026.04.12.12

- Multi Controller settings-UI gebruikt nu een echte aan/uit-toggle: uit = standalone lokaal, aan = Multi Controller actief
- Bij uitschakelen worden de Multi Controller-instellingen verborgen; bij inschakelen komt automatisch de laatst gekozen rol (master/slave) terug
- Laatst gekozen Multi Controller-rol wordt nu apart onthouden zodat uitschakelen niet meer terugvalt naar een vergeten standalone-keuze
- Multi Controller-config validatie wordt niet meer onnodig afgedwongen terwijl de functie uit staat

## v2026.04.12.11

- MQTT/HA fix: kerntelemetrie (`Boiler PWM`, `Netvermogen`, `Overschot`, `Fout`, `Legionella temp`, `WiFi RSSI`, `Uptime`) krijgt nu eigen lichte `live/.../state` topics met dezelfde discovery object ids en namen
- HA discovery voor die bestaande sensoren blijft backward compatible, maar gebruikt voortaan de lichte losse topics in plaats van alleen `value_json` uit `<base>/status`
- `<base>/status` in Multi Controller-modus afgeslankt door de zware `mc_peer_outputs` en `mc_local_pwm` arrays uit MQTT-status te halen; de aparte `mc/...` topics blijven bestaan

## v2026.04.12.10

- Multi Controller fasebeveiliging omgezet naar het besproken model:
  - standalone blijft 1 lokale fasekeuze
  - master kan nu ook netwerk-controllers aan een fase koppelen
  - ampèrelimieten worden nu duidelijk per fase (L1/L2/L3) ingesteld
- Master-groepsplanner begrenst nu ook netwerkdeelnemers op basis van fasebelasting op de mastermeter
- Slave houdt in netwerk-volgmodus een lokale harde noodrem op zijn eigen fase, zonder een tweede groepsplanner te worden
- Multi Controller UI verduidelijkt dat de master leidt en de slave alleen lokaal noodbeveiligt


- Multi Controller bugfix master/slave Force Heat: in netwerk-mastermodus past de master zijn eigen lokale planner-targets nu niet meer alleen bij `SURPLUS` toe, maar ook bij groepsscenario's zoals `FORCE_HEAT` en `PRIORITY_HEAT`.
- Daardoor volgt de lokale master-output nu hetzelfde groepsplannerpad als de netwerkdeelnemers, zodat lokaal en remote bij force/prioriteitsmodi weer gelijk gedrag vertonen.
- Minimale gerichte fix: standalone-regeling, peer-HTTP en de rest van het versnellingsplan zijn ongemoeid gelaten.

## v2026.04.12.08

- Multi Controller UI-bugfix: bij het omschakelen van controller 2/3 naar `Netwerk-controller` wordt het IP-veld nu direct vrijgegeven zonder eerst op te slaan of de pagina te herladen.
- De settings-UI schakelt nu live tussen netwerk-IP en lokale PWM-pin, zodat `Netwerk` meteen het IP-veld toont en `Lokaal` meteen de GPIO-keuze terugzet.
- Alleen de Multi Controller settings-UI aangepast; standalone-regeling, planner en peerlogica ongemoeid gelaten.

## v2026.04.12.07

- Multi Controller versnellingsplan stap 7 uitgevoerd: externe bereikbaarheid in Multi Controller-modus verder verbeterd zonder de standalone-modus aan te raken.
- `loop()` bedient de webserver nu ook extra rondom de Multi Controller-pass en na overige periodieke taken, zodat UI en externe requests minder stroperig aanvoelen terwijl peers actief zijn.
- Peer-HTTP voert nu ook expliciete cooperative yields rond blokkerende GET/POST-calls uit, zodat de ESP/WiFi-stack sneller lucht krijgt tijdens netwerkwerk.
- De dashboard live-route `/api/live` stuurt voor Multi Controller-slots alleen nog de velden die de live-chips echt gebruiken (`slot`, `name`, `duty_percent`), waardoor minder JSON, parsing en netwerkbelasting ontstaat bij open dashboard.

## v2026.04.12.06

- Multi Controller versnellingsplan stap 6: groepsplanner vereenvoudigd voor de nieuwe randvoorwaarden met netwerk alleen achteraan.
- Master-planner telt netwerk-slots nu alleen nog mee wanneer de peer ook echt bruikbaar is voor writeback/status; een geldig IP alleen is niet meer genoeg.
- Vrijgave van volgende deelnemers wacht niet meer op een extra bevestigde peerstatus ná iedere writeback; de planner gebruikt de laatst bruikbare cached peerstatus/groepsmodelstatus.
- Houdt de opregel-/terugregelvolgorde overeind, maar haalt onnodig wachten uit de netwerk-keten.

- Multi Controller versnellingsplan stap 5: nieuwe compacte interne peerstatus-route `GET /api/mc/peer_status` toegevoegd voor master/slave-uitwisseling.
- Peer-poll leest nu niet langer `/api/status_light`, maar alleen nog de compacte Multi Controller-status met de gegevens die de planner echt nodig heeft.
- De compacte peerstatus bevat alleen Multi Controller-relevante velden en per output o.a. gevraagde/toegewezen PWM, blokkering en blokkade-reden.
- Standalone-regeling opnieuw ongemoeid gelaten.

## v2026.04.12.04

- Multi Controller versnellingsplan stap 4 uitgevoerd: peer-poll gebruikt nu nog maar **één** lichte HTTP-call naar `/api/status_light`; de extra `/api/ping`-ronde is uit de peer-flow gehaald.
- Peer HTTP timeout bewust sterk verlaagd van **2500 ms** naar **900 ms**, zodat een haperende peer de hoofdloop veel korter ophoudt.
- Identieke remote target-payloads worden niet meer steeds opnieuw gepusht; dezelfde payload wordt nu pas na een ruimere refresh opnieuw verstuurd om de remote TTL gezond te houden.
- Na een succesvolle target-push wordt een statuspoll nog wel snel ingepland, maar niet meer in exact dezelfde service-tik geforceerd.
- Multi Controller UI toont bij netwerk-slots nu alleen nog de relevante **status HTTP** code in plaats van oude ping/status dubbeltelling.

## v2026.04.12.03

- Multi Controller versnellingsplan stap 3 uitgevoerd: de snelle Multi Controller-pass draait nu eerst lokaal door voor coordinatorstatus, groepsmodel en planner, zonder direct te wachten op blokkerende peer-HTTP.
- Nieuwe tragere netwerkservice toegevoegd: per service-tik wordt hooguit één peer-actie uitgevoerd (poll of target-writeback), zodat de hoofdloop niet meer bij elke iteratie aan netwerk-I/O hangt.
- Failsafe/peer-housekeeping draait nu los van de snelle pass op een eigen kort interval en wordt na echte netwerkactiviteit direct nog eens bijgewerkt, zodat offline/TTL-terugval wel actueel blijft zonder de snelle lus te verzwaren.
- Peer-writeback helper opgeschoond zodat hij een echte actie terugmeldt aan de scheduler; daardoor kan de nieuwe netwerkservice writes en polls gecontroleerd afwisselen.

## v2026.04.12.02

- Multi Controller versnellingsplan stap 2: netwerk-controllers mogen nu alleen nog op de laatste actieve positie(s) staan. Opslaan van combinaties zoals lokaal → netwerk → lokaal wordt geblokkeerd en de bestaande werkende config blijft dan behouden.
- Multi Controller UI toont nu expliciet deze nieuwe volgorderegel in de instellingen.

- Multi Controller versnellingsplan stap 1 uitgevoerd: het totaal aantal controllers is nu bewust teruggebracht van 4 naar maximaal 3 (controller 1 + maximaal 2 extra controllers).
- `MC_MAX_SLOTS` en alle actieve slot-arrays/clamps zijn daarop aangepast, zodat oude configuraties en invoer boven 3 veilig naar 3 worden begrensd.
- Multi Controller UI toont nu alleen controller 1 t/m 3 en gebruikt overal grenzen van 1..3 voor groepsgrootte en op/afregelvolgorde.
- Controller 4 is uit de actieve Multi Controller-config/defaults verwijderd, zodat planner, status en telemetrie niet langer een vierde slot meeslepen.

## v2026.04.11.23

- Multi Controller netwerkvrijgave verder aangescherpt: een master pusht nu niet steeds opnieuw exact dezelfde peer-targets zolang nog geen verse statusbevestiging is teruggekomen. Daardoor kan een netwerkcontroller die vóór een lokale controller staat zijn vrijgavegrens eerst echt bevestigen, in plaats van dat de lokale controller blijft wachten door een steeds vernieuwde write-timestamp.
- Na een succesvolle peer-target writeback wordt direct een nieuwe peerstatus-poll ingepland, zodat de master sneller bevestigde netwerk-PWM terugkrijgt voor de vrijgavelogica.
- Writeback-interval gelijkgetrokken aan het poll-interval om de netwerkvolgorde stabieler te maken.

## v2026.04.11.22

- Multi Controller volgorde fix: **Volgende mee vanaf PWM %** is nu hard afgedwongen voor zowel lokale als netwerkcontrollers.
- Een volgende controller wordt nu pas vrijgegeven als de vorige controller de ingestelde vrijgavegrens **echt gehaald heeft**.
- Eerder doorschakelen mag alleen nog wanneer de vorige controller **bevestigd** op een echte harde limiet vastzit, zoals **ingestelde max PWM** of **beveiliging/fase-limiet**.
- Generiek `blocked` of `geen headroom` zonder bevestigde max/safety-reason geeft de volgende controller dus niet meer automatisch vrij.

## v2026.04.11.20

- Multi Controller netwerkvolgorde fix: **Volgende mee vanaf PWM %** gebruikt op de master nu voor netwerkcontrollers alleen nog een **verse, bevestigde slave-terugmelding na de laatste master-writeback**.
- Daardoor mag een volgende lokale controller niet meer alvast meelopen op basis van oude netwerkstatus terwijl de netwerkcontroller de vrijgavegrens intussen niet meer haalt.
- Master groepsmodel toont voor netwerkcontrollers nu waar beschikbaar de **output-specifieke toegepaste/requested PWM, block/headroom en block-reason** van de slave-output in plaats van alleen de grove peer-groepwaarde.
- Netwerkrespons iets sneller gemaakt voor strakkere volgorde: peer poll interval verlaagd naar **2000 ms** en target writeback interval naar **750 ms**.

## v2026.04.11.19

- Multi Controller / slave: nieuwe optie **Lokale legionella mag master overrulen** toegevoegd voor slave-modus
- legionella: slave kan nu zijn bestaande lokale legionella-programma autonoom draaien op basis van eigen sensoren en 7-dagenlogica wanneer die override expliciet is toegestaan
- legionella UI: onder **Run** wordt nu zichtbaar wanneer **Master override actief: Legionella** is
- slave-beveiliging: zonder expliciete override blijft de master leidend en start de slave geen lokale legionella-run

## v2026.04.11.18

- dashboard: bovenin Live status staat nu een duidelijke badge met de actieve Multi Controller-rol: Standalone, Master of Slave
- UI: de rolbadge wordt server-side direct goed getoond en daarna live bijgewerkt via `/api/status_light`

- dashboard fix: de actieve Multi Controller chips op het dashboard tonen nu ook netwerk-controllers op de master
- dashboard fix: controller 1 wordt niet meer dubbel getoond in de extra Multi Controller chips, omdat die al via de bestaande hoofd-PWM/UI zichtbaar is
- dashboard fix: in slave-modus blijft de extra chiprij daardoor leeg zolang er geen extra lokale controllers naast controller 1 actief zijn

## v2026.04.11.15

- Multi Controller fix: NORMAL surplus in master/slave gebruikt nu een directe closed-loop regeling op P1 richting `targetExportW`.
- De master rekent bij zonne-overschot niet meer eerst een absolute groeps-PWM uit `targetExportW / maxBoilerPowerW`.
- In plaats daarvan verhoogt of verlaagt de master de actuele groeps-output nu stap voor stap op basis van de P1-fout en de bestaande regeltuning (`deadband`, `stepFactor`, `max step`).
- Alleen veiligheidslagen zoals fasebeveiliging en ingestelde PWM-limieten mogen daarna nog begrenzen.
- Multi Controller UI-tekst verduidelijkt van `Totale groeps-PWM gevraagd` naar `Totale groeps-output doel`.

## v2026.04.11.14

- Multi Controller fix: in netwerk-slave-modus wordt de lokale per-controller phase-guard niet meer toegepast.
- De slave reset daarbij zijn lokale phase-cap-state, zodat hij een master-target niet meer onterecht lokaal op bijvoorbeeld 40% vasthoudt.
- Fasebewaking en groepsverdeling blijven in slave-modus nu leidend bij de master.

## v2026.04.11.13

- Multi Controller opgeschoond: oude coordinator-kandidaat/restcode verwijderd; master/slave-rol is nu de enige leidende netwerklogica.
- Oude apply_step scripts uit eerdere tussenstappen verwijderd om de release-map schoon te maken.
- Master-slave Multi Controller stap 10: status en telemetry opgeschoond voor de nieuwe master-slave architectuur
- nieuwe rolgerichte telemetry toegevoegd: `mc_role`, `mc_control_source`, master netwerk-slot statusvelden en slave master-link statusvelden
- bestaande legacy status_light velden blijven nog aanwezig voor compatibiliteit met de huidige peer/uitleescode

## v2026.04.11.11

- Multi Controller UI opgeschoond voor het nieuwe master-slave model
- master toont nu eenvoudige netwerk-slot en groepsplannerinformatie zonder oude coordinator/peer-terminologie als kern van de regeling
- slave toont nu vooral master-link en remote target-control status in plaats van oude peer/coordinator-details
- netwerk-slots in de UI spreken nu over master/slave en setpoint-push, zodat de schermteksten beter passen bij de nieuwe architectuur

## v2026.04.11.10

- Master-slave Multi Controller stap 8: oude peer/coordinator-restlogica verder buitenspel gezet; de master wacht niet meer op compacte peerstatus of groepsnaam-match om netwerk-slots te plannen en setpoints te pushen, en de slave houdt zijn groepsmodel bewust lokaal.

## v2026.04.11.08

- master-slave Multi Controller stap 6: slave volgt in netwerkmodus bij force/prioriteitsscenario's nu alleen nog master-doelen en niet meer zijn eigen lokale interpretatie; force/boost/piority blijven daarmee master-led terwijl lokale standalone werking ongemoeid blijft

## v2026.04.11.07

- Multi Controller master-slave stap 5: netwerk-slots op de master tellen nu direct mee als remote deelnemers in het groepsmodel
- planning op de master wacht daardoor niet meer op compacte peer-outputstatus van slaves
- remote target-writeback bouwt nu op de master-slotconfig in plaats van op wederzijdse peer-planning

## v2026.04.11.06

- Master-slave Multi Controller stap 4: eenvoudige master→slave protocolbasis gelegd
- master is nu altijd leidend in master-modus en pusht setpoints naar netwerk-slots zonder coordinator-verkiezing
- slave accepteert remote targets alleen nog van het geconfigureerde master-IP
- master-push is minder afhankelijk van peer-health; heartbeat/status_light blijft alleen lichte terugmelding

## v2026.04.11.05

- master-slave Multi Controller stap 3: slave-modus vastgezet op controller 1 lokaal + controller 2 als master-link
- slave plant niet meer zelf in de Multi Controller-loop en wacht op master-doelen
- slave valt niet meer terug op eigen lokale groepsverdeling als er nog geen verse master-targets zijn
- slave-UI vereenvoudigd: vaste 2-controller opzet, vaste master-koppeling, controller 3 verborgen

## v2026.04.11.04

- Master-slave Multi Controller stap 2: master-slots vereenvoudigd; controller 2..3 kies je nu per slot als lokaal of netwerk, en de actuele architectuur wordt op de master automatisch uit de slottypes afgeleid
- UI: peer-IP is alleen nog actief voor netwerk-slots; lokale PWM-pin is alleen nog actief voor lokale slots
- Slave-rol wordt in deze tussenstap intern op netwerkbasis vastgezet als voorbereiding op de echte luister-/uitvoermodus in stap 3

## v2026.04.11.03

- Multi Controller stap 1 basis voor de nieuwe master/slave-architectuur: nieuwe ESP-rolkeuze `Standalone lokaal / Master / Slave` toegevoegd aan config en UI
- bestaande lokale werking blijft behouden; `Standalone lokaal` zet netwerk-Multi Controller uit zonder bestaande slotconfiguratie weg te gooien
- huidige slotarchitectuur (`lokaal / netwerk / hybride`) blijft in deze tussenstap nog bestaan voor compatibiliteit; verdere master/slave-ombouw volgt in volgende stappen

## v2026.04.11.02

- Multi Controller netwerk/hybride: peers blijven bij korte `status_light`-haperingen langer bruikbaar voor cached compacte peerstatus en remote target-writeback
- voorkomt dat followers na een korte peerstatus-misser te snel zonder verse remote targets vallen en daarna terugvallen/pendelen
- groepsmodel en writeback gebruiken nu recente bruikbare peerstatus robuuster in plaats van direct alles te blokkeren op `statusOk=false`

## v2026.04.11.01

- Multi Controller netwerk/hybride: follower-ESP houdt peer/coordinator nu langer vast via een coordinator-holdtijd op basis van de laatste geldige peerstatus
- korte peer/status-haperingen maken een follower daardoor niet meer direct opnieuw lokale coordinator tijdens NORMAL overschot
- coordinator-keuze gebruikt tijdelijke cached peerstatus zolang die nog binnen de holdtijd geldig is, zodat master/follower minder snel tegen elkaar in gaan regelen

## v2026.04.10.32

- Multi Controller netwerk/hybride: peer-ESP's respecteren nu ook de lokale fasebeveiliging softstartduur bij force/prioriteitswarmte; follower target net power grijpt bij NORMAL niet meer in tegen de coordinator.

## v2026.04.10.31

- Multi Controller netwerk/hybride: niet-coordinator peers vallen bij NORMAL zonne-overschot niet meer tijdelijk terug op een eigen lokale groepsverdeling; zij wachten nu op verse remote overschotdoelen van de coordinator.


- Multi Controller netwerk-force/prioriteitswarmte aangepast voor hybride + peer-IP opstelling: toegestane remote deelnemers krijgen nu hun eigen ingestelde slot-maximum in plaats van een verdeelde groeps-PWM
- bestaande lokale werking ongemoeid gelaten; wijziging geldt alleen in de groepsplanner voor netwerk-peers
- lokale veiligheidslagen blijven leidend op iedere ESP afzonderlijk

## v2026.04.10.28

- Multi Controller peer-link stabiliteit verbeterd zonder de bestaande architectuur open te breken
- peer HTTP timeout verhoogd van 1200 ms naar 2500 ms
- peer poll-interval verhoogd van 5000 ms naar 7000 ms om minder piekbelasting tussen ESP's te geven
- peer stale/offline-venster verruimd zodat een korte hapering minder snel tot offline leidt
- enkele target-writeback timeout markeert de peer niet meer direct offline; de normale peer-poll beslist dat nu
- offline-status wist nu ook de oude ping/status HTTP-codes, zodat het scherm niet meer tegelijk 200/200 én offline laat zien
- status helper `multiControllerGetActiveLocalPwmCount()` telt nu echt actieve lokale outputs in plaats van blind de effectieve teller door te geven

## v2026.04.10.27

- UI polish: in Multi Controller settings, the `Peer IP-adres` field for **Controller 1** is now shown disabled/grayed out
- Controller 1 remains the fixed local output on **GPIO25**; backend logic and persistence stay unchanged
- prevents accidental IP entry on Controller 1 without changing the existing Multi Controller code path

## v2026.04.10.26

- Gerichte compile-fix voor ontbrekende linker-definities na modulair opsplitsen (temperatuurhelpers, legionella-output, reset reason en app-bridges).

## v2026.04.10.25

- Compile-fix na opsplitsing: sunWinDate typeconflict hersteld in HealthMonitor.
- Compile-fix: mcIsValidIpv4 gedeeld gemaakt voor MultiController config-validatie.
- Compile-fix: fwUrlsConfigured en forward declaration voor handleFwUpdateNowUi toegevoegd in FirmwareUpdateManager.
- Compile-fix: mcResetPeerRuntime extern/static conflict opgelost in MultiControllerPeers.

## v2026.04.10.24

- compile fix: shared `GAS_DAY_START_HOUR` via `src/TimeSchedule.h` so the main `.ino` can still use the gas-day start constant after the TimeSchedule module split

## v2026.04.10.23

- Maintenance: Multi Controller API/telemetry JSON helpers moved into new module `src/MultiControllerTelemetry.h/.cpp`.
- Moved compact peer output JSON, local PWM JSON and the repeated Multi Controller status payload builders out of the main `.ino` and `src/HealthMonitor.cpp`.
- No intended control-behaviour change; this is a structural split to keep Multi Controller status/API telemetry grouped together.

## v2026.04.10.22

- Maintenance: internal Multi Controller coordinator/orchestrator loop moved into `src/MultiControllerCoordinator.cpp`.
- `src/MultiController.cpp` now mainly holds shared runtime state and basic helpers, while the peer/coordinator loop lives in its own source file.
- No intended control-behaviour change; this is a structural split to make further Multi Controller maintenance cleaner.

## v2026.04.10.21

- Multi Controller intern verder opgesplitst met nieuw bestand `src/MultiControllerRuntime.cpp`.
- Uit `src/MultiController.cpp` gehaald: lokale PWM-runtime init/reconfigure, scenario-resolutie, scenario-eligible slot helpers en apply/requested-group-percent logica.
- `src/MultiController.cpp` houdt nu vooral de gedeelde runtime-state en de peer/coordinator-loop.
- Geen bedoelde functiewijziging; dit is een onderhoudsstap om de resterende core van Multi Controller verder te verkleinen.

## v2026.04.10.20

- Multi Controller intern verder opgesplitst met nieuw bestand `src/MultiControllerGroupModel.cpp`.
- Uit `src/MultiController.cpp` gehaald: groepsmodel-opbouw, reset/sort helpers en scenario-label helper.
- `src/MultiControllerInternal.h` uitgebreid met gedeelde declaraties voor groepsmodel/helpers.

## v2026.04.10.19

- onderhoud: verdere interne opsplitsing van `MultiController` met nieuwe module `src/MultiControllerPlanner.cpp`
- planner-/distributielogica uit `src/MultiController.cpp` gehaald: parallel priority targets, ramp-up/down verdeling, groepsplanner en per-controller phase-guard toepassing
- `src/MultiControllerInternal.h` uitgebreid met gedeelde planner-declaraties voor deze interne split
- geen bedoelde functiewijziging; dit is een structuurstap om verdere interne opsplitsing van `MultiController` veiliger en overzichtelijker te maken

## v2026.04.10.18

- Multi Controller intern verder opgesplitst met nieuwe module `src/MultiControllerConfig.cpp`.
- Config/persistence/apply-logica uit `src/MultiController.cpp` gehaald: defaults, clamp, load/save en settings-apply.
- Gedeelde interne Multi Controller helpers en pinconstanten gecentraliseerd in `src/MultiControllerInternal.h`.
- Voorbereid op verdere interne opsplitsing zonder bewust regelgedrag te veranderen.

## v2026.04.10.17

- internal Multi Controller split: moved the Multi Controller settings/UI builder out of `src/MultiController.cpp` into new file `src/MultiControllerUi.cpp`
- also moved the local UI HTML helpers for this block (`mcHtmlEscape`, `mcSelInt`, `mcAppendYesNoSelect`) together with the settings section builder
- no intended behavior change; this is a structure/maintenance step to keep the main Multi Controller runtime/planning file focused on runtime logic

## v2026.04.10.16

- onderhoud: tweede interne opsplitsing van `MultiController` door peer/netwerk-coördinatie te verplaatsen naar nieuwe module `src/MultiControllerPeers.cpp`
- nieuw: `src/MultiControllerInternal.h` uitgebreid met gedeelde peer/network-constanten en interne helper-declaraties voor polling, coordinator-keuze en remote-target writeback
- geen bedoelde functiewijziging; dit is een structuurstap om verdere interne opsplitsing van `MultiController` veiliger en overzichtelijker te maken

## v2026.04.10.15

- onderhoud: eerste interne opsplitsing van `MultiController` door runtime-/status-/peer-health getters te verplaatsen naar nieuwe module `src/MultiControllerStatus.cpp`
- nieuw: gedeelde interne header `src/MultiControllerInternal.h` voor Multi Controller runtime-structs en interne state die nu door meerdere Multi Controller bronbestanden gebruikt wordt
- geen bedoelde functiewijziging; dit is een structuurstap om verdere interne opsplitsing van `MultiController` veiliger te maken

## v2026.04.10.14

- maintenance/refactor: moved time/date/gas-day and sun-schedule helpers out of the main `.ino` into new `src/TimeSchedule.h/.cpp`
- includes time validity helpers, local date formatting, legionella next-run calculation, gas-day ISO helpers and cheapest-daylight window planning
- keeps the existing scheduling behaviour functionally the same while reducing time/schedule logic in the main firmware file

## v2026.04.10.13

- Maintenance: meter readers and meter helper logic moved into `src/MeterManager.h/.cpp`.
- Moved meter label helper, Youless/HomeWizard/Smart Gateways power/phase readers, selected-meter wrappers and the Smart Gateways phase-refresh helper out of the main `.ino`.
- Keeps intended meter/control behaviour the same while making the meter integration easier to maintain.

## v2026.04.10.12

- maintenance/refactor: moved WiFi/AP/NTP/hostname helpers into new `src/NetworkManager.h/.cpp`
- includes STA connect, setup AP, hostname sanitising/validation, mDNS startup and grouped WiFi scan route registration
- keeps existing network behaviour functionally the same while reducing WiFi/bootstrap glue in the main firmware file

## v2026.04.10.11

- Maintenance: firmware update / OTA logic moved into `src/FirmwareUpdateManager.h/.cpp`.
- Moved GitHub OTA helpers, update progress/status endpoints, update pages/upload handlers and the auto-check tick out of the main `.ino`.
- Added `registerFirmwareUpdateRoutes()` so OTA web/API routes are now grouped in the new module instead of being split across the main file and `src/WebRoutes.cpp`.
- Keeps intended firmware-update behaviour the same while reducing OTA-specific maintenance code in the main firmware file.

## v2026.04.10.10

- compile fix: shared `FW_VERSION` via `src/app_globals.h` so `src/HealthMonitor.cpp` can access the firmware version after the health module split
- no intended behavior change; this is a targeted build repair after v2026.04.10.09

## v2026.04.10.09

- compile fix: exported `appendMultiControllerCompactPeerOutputs(...)` via `HealthMonitor` so `mqttPublishStatusJson()` can still append compact peer output data
- compile fix: restored missing hostname helpers `sanitizeHostname(...)` and `isValidHostname(...)` used by settings save and mDNS startup

- Maintenance: health/diagnostics helpers and status/system endpoints moved into `src/HealthMonitor.h/.cpp`.
- Moved `/api/status`, `/api/status_light`, `/api/live`, `/api/system`, `mqttPublishSystemJson()` and `fmtUptimeDHMM()` out of the main `.ino`.
- Added `registerHealthRoutes()` so health/status route registration is now grouped in the new module.
- Keeps intended runtime/control behaviour the same while reducing diagnostic/status code in the main firmware file.

## v2026.04.10.07

- Maintenance: MQTT callback/reconnect orchestration further centralised in `src/mqtt.cpp`.
- Moved the thin MQTT callback router out of the main `.ino` into the MQTT module.
- Added shared helpers for topic subscription and retained-state republish on MQTT reconnect and Home Assistant birth messages, so the same publish bundle is no longer duplicated in two places.
- Keeps intended MQTT/HA behaviour the same while reducing duplicated orchestration code in the main firmware file.

## v2026.04.10.06

- Maintenance: shared Web/UI helpers and web route registration moved into `src/WebRoutes.h/.cpp`.
- Moved shared HTML/layout helpers (`htmlCss`, `htmlHeader`, `navHtml`, `htmlFooter`, `rowHtml`, `htmlAutoReturnAfterReboot`) and the Nordpool prices page out of the main `.ino`.
- Centralised page/UI route registration in `registerWebRoutes()` while leaving dashboard/control/settings page logic in the main `.ino` for a safe intermediate split.
- Reduced the main `.ino` further without intended behaviour changes.

## v2026.04.10.05

- Maintenance: scenario/control helpers moved into `src/ScenarioLogic.h/.cpp`.
- Moved priority-mode detection, scenario selection, normal control helpers, force/price-boost apply helpers, and heat-compare execution logic out of the main `.ino`.
- Kept schedule/window logic in the main `.ino` for now, so this remains a safe structural split without intended behaviour changes.

## v2026.04.10.04

- maintenance/refactor: moved phase-guard helpers out of the main `.ino` into new `src/PhaseGuard.h/.cpp`
- includes phase-current lookup, configured max-A lookup, legacy max refresh and the phase-guard decision logic
- keeps the existing safety behaviour intact while making the guard logic easier to maintain

## v2026.04.10.03

- maintenance/refactor: moved PWM/output routing helpers out of the main `.ino` into new `src/PwmOutput.h/.cpp`
- includes `pwmInit()`, `pwmWriteDuty()`, scenario/group duty routing and phase-guard output limiting helpers
- keeps the existing control behaviour intact while making the output layer easier to maintain

## v2026.04.10.02

- maintenance/refactor: moved REST API helpers and control/config endpoints out of the main `.ino` into new `src/ApiRoutes.h/.cpp`
- includes relay API, config API, force-heat/legionella/reboot endpoints and firmware auto-update toggle endpoint
- keeps route behaviour functionally the same while reducing API/control code in the main firmware file

## v2026.04.10.01

- maintenance/refactor: moved `clampAll()`, `loadConfig()` and `saveConfig()` out of the main `.ino` into new `src/ConfigStore.h/.cpp`
- keeps behaviour functionally the same while reducing config/persistence code in the main firmware file
- `fwAutoUpdateEnabled` is now shared as a normal global so config persistence can live in the module

## v2026.04.08.19

- compile fix: `appendMultiControllerCompactPeerOutputs` now uses `JsonDocument&` instead of a template parameter that broke Arduino compilation in the `.ino`
- no functional Multi Controller logic changes

