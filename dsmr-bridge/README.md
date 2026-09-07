# DSMR Reader -> HomeWizard P1 bridge

Deze lokale bridge leest een DSMR Reader-installatie uit en biedt het minimale
HomeWizard P1-endpoint aan dat de Solar Controller gebruikt:

```text
GET /api/v1/data
```

De bridge levert onder andere:

- `active_power_w`
- `active_current_l1_a`
- `active_current_l2_a`
- `active_current_l3_a`
- `active_power_l1_w`, `active_power_l2_w`, `active_power_l3_w`

De Solar Controller kan daarna worden ingesteld op **HomeWizard P1** met het IP-adres
van deze bridge.

## Installatie

De bridge gebruikt alleen Python 3 uit de standaardbibliotheek.

```sh
cd dsmr-bridge
python3 -m unittest -v
```

Starten:

```sh
DSMR_READER_URL='http://192.168.15.233:7777/api/v2/datalogger/dsmrreading?limit=1&ordering=-timestamp' \
DSMR_READER_API_KEY='vul-je-api-key-in' \
python3 bridge.py
```

De bridge gebruikt de DSMR Reader REST API met de **`X-AUTHKEY`**-header. Het
endpoint `/api/v2/datalogger/dsmrreading` geeft een gepagineerde lijst terug;
`ordering=-timestamp` zorgt dat de nieuwste meting bovenaan staat.

De service luistert standaard op `0.0.0.0:80`, zodat de Solar Controller alleen
het IP-adres van de bridge nodig heeft. Voor testen zonder rootrechten kun je
`BRIDGE_PORT=8088` gebruiken.

## Configuratie

| Variabele | Standaard | Betekenis |
|---|---:|---|
| `DSMR_READER_URL` | `/api/v2/datalogger/dsmrreading?limit=1&ordering=-timestamp` | DSMR Reader-endpoint |
| `DSMR_READER_API_KEY` | leeg | API-key; verstuurd als `X-AUTHKEY`-header |
| `DSMR_READER_TIMEOUT_S` | `5` | HTTP-timeout |
| `POLL_INTERVAL_S` | `5` | Interval waarmee DSMR Reader wordt uitgelezen |
| `DSMR_POWER_UNIT` | `kw` | Eenheid van DSMR-vermogensvelden: `kw` of `w` |
| `PHASE_VOLTAGE_V` | `230` | Fallback-voltage als DSMR Reader geen fasevoltage rapporteert |
| `STALE_AFTER_S` | `30` | Daarna geeft de bridge HTTP 503 terug |
| `BRIDGE_HOST` | `0.0.0.0` | Luisteradres |
| `BRIDGE_PORT` | `80` | Luisterpoort |
| `BRIDGE_UNIQUE_ID` | `dsmr-reader-bridge` | Identificatie in de response |

De DSMR Reader REST API moet in DSMR Reader zijn ingeschakeld. Controleer de
werkelijke API-URL en veldnamen van jouw installatie via het Support-menu van
DSMR Reader. De bridge accepteert zowel een object als een lijst in `results`,
`data` of `reading`.

## Testen

```sh
curl http://127.0.0.1:80/health
curl http://127.0.0.1:80/api/v1/data
```

Een stale of ontbrekende DSMR-meting geeft bewust `503 Service Unavailable`.
Zo kan de Solar Controller de onbetrouwbare meting veilig behandelen in plaats
van onbeperkt oude data te gebruiken.

## Draaien op de Raspberry Pi (Podman)

De bridge draait als eigen container naast de DSMR Reader-container (de
`dsmr`-container, hier gepubliceerd op hostpoort `7777`). De Solar Controller
blijft daardoor ongewijzigd en gebruikt gewoon HomeWizard P1 met het IP-adres
van de Pi.

Voorbeeldarchitectuur:

```text
Slimme meter -> dsmr (podman, :7777) --\
                                        +-> bridge (:8080) -> /api/v1/data
                  bridge (podman)  ----/                         ^
Solar Controller (ESP32) kiest HomeWizard P1 op http://192.168.15.233 ----+
```

Getest op podman 4.3.1:

- Quadlet wordt **niet** ondersteund (podman < 4.4).
- `podman-compose` hangt bij het starten van deze container.
- Gebruik daarom `podman run` + `podman generate systemd`.

### Bestanden

- `Containerfile` — image voor de bridge
- `deploy/podman/dsmr-homewizard-bridge.env.example` — voorbeeldconfiguratie (zonder key)
- `deploy/podman/dsmr-homewizard-bridge.service` — werkende systemd-user-unit

### Installatie op de Pi (als gebruiker `dsmrreader`)

```sh
# 1. Image bouwen
cd ~/dsmr-bridge
podman build -t localhost/dsmr-homewizard-bridge .

# 2. bridge.env aanmaken (kopie van .env.example, vul de API-key in)

# 3. Container starten (env-bestand met DSMR-instellingen)
podman run -d --name dsmr-homewizard-bridge \
  --restart always -p 8080:80 \
  --env-file ~/dsmr-bridge/bridge.env \
  localhost/dsmr-homewizard-bridge

# 4. Systemd-unit genereren en installeren (overleeft reboot)
podman generate systemd --new --name dsmr-homewizard-bridge \
  > ~/.config/systemd/user/dsmr-homewizard-bridge.service
export XDG_RUNTIME_DIR=/run/user/1002
systemctl --user daemon-reload
systemctl --user enable --now dsmr-homewizard-bridge.service
loginctl enable-linger dsmrreader   # starten na reboot
```

### Controle

```sh
podman ps                              # container 'dsmr-homewizard-bridge' moet Up staan
curl http://127.0.0.1:8080/health      # {"ok":true,...}
curl http://127.0.0.1:8080/api/v1/data
journalctl --user -u dsmr-homewizard-bridge.service -f
```

### Poort 80 of 8080?

De container publiceert standaard hostpoort **8080**. Als de Solar Controller
HomeWizard P1 zonder poortnummer gebruikt (vaste poort 80), wijzig dan `-p
8080:80` in `-p 80:80`. Rootless Podman mag standaard geen poorten onder 1024
binden; los dat op met:

```sh
sudo sysctl -w net.ipv4.ip_unprivileged_port_start=80
```

(of voeg dit toe aan `/etc/sysctl.d/99-unprivileged-ports.conf` en herstart).

