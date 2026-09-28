# Server so stavmi relé

Zbiera hlásenia z Opty a ukazuje stav všetkých 12 relé (4 interné + 8 na
RS485). Opta je klient — sama sem posiela POST, takže to funguje aj z inej
siete a netreba presmerovať žiadny port smerom k nej.

```
Opta ──POST /api/report každých 5 s──▶ server ──dashboard──▶ prehliadač
```

Bez závislostí, len vstavané moduly Node.js.

## Spustenie

```bash
cd server
cp .env.example .env
# vyplň DEVICE_TOKEN, ADMIN_PASSWORD, SESSION_SECRET:
#   openssl rand -hex 24
docker compose up -d
```

Beží na `http://<ip-servera>:8080`. Prihlás sa heslom z `ADMIN_PASSWORD`.

Server odmietne štart, kým nie sú vyplnené tokeny — radšej nenabehne, než
by bežal dokorán otvorený.

## Nastavenie Opty

V `opta_client/opta_client.ino` uprav:

```cpp
const char* SERVER_HOST  = "192.168.88.10";   // IP servera
const int   SERVER_PORT  = 8080;
const char* DEVICE_TOKEN = "...";             // rovnaké ako v .env
const char* DEVICE_NAME  = "opta-hala";       // ukáže sa na dashboarde
```

Do Serial Monitora vypíše `hlasenie OK`. Ak `server odmietol: ... 401`,
nesedí token.

## API

| Endpoint | Kto | Čo |
|---|---|---|
| `POST /api/report` | Opta | hlásenie stavov, hlavička `X-Device-Token` |
| `GET /api/state` | dashboard | posledný stav všetkých zariadení |
| `GET /api/history` | dashboard | posledných 100 **zmien** |
| `GET /healthz` | docker | kontrola behu |

Telo hlásenia:

```json
{"device":"opta-hala","modbusOk":true,
 "ext":[1,0,1,0,0,0,0,1],"int":[0,1,0,0]}
```

`ext` je 8 relé na RS485, `int` sú 4 na Opte. Server dĺžku polí kontroluje
a čokoľvek iné odmietne.

Do histórie sa zapisujú len **zmeny**, nie každé hlásenie — inak by pri
päťsekundovom intervale narástla o 17 000 záznamov denne bez informácie.

## Keď pribudne doména

Server samotný nerieši HTTPS. Daj pred neho reverzné proxy, ktoré si
certifikát vybaví samo — napríklad Caddy:

```
relay.mojafirma.sk {
    reverse_proxy relay-server:8080
}
```

Potom v `docker-compose.yml` zruš mapovanie portu `8080:8080` (nech sa
server nedá obísť priamo) a v skici nastav:

```cpp
const char* SERVER_HOST = "relay.mojafirma.sk";
const int   SERVER_PORT = 443;
const bool  USE_HTTPS   = true;
```

**Toto nie je voliteľné.** Cez HTTP ide `DEVICE_TOKEN` po sieti čitateľný;
v rámci LAN sa to dá zniesť, cez internet nie. Opta HTTPS zvláda
(`EthernetSSLClient` je v jadre Opty).

## Prihlásenie a SSO

Teraz je `AUTH_MODE=password`, teda jedno spoločné heslo. Je to dočasné
riešenie, aby sa dalo pracovať.

Napojenie na SSO („kardinal") je pripravené ako jedna vrstva — funkcie
`loggedIn()` a `authRoutes()` v `server.js`. Vymení sa len ich obsah,
zvyšok aplikácie o spôsobe prihlásenia nevie nič.

Na napojenie potrebujem vedieť: adresu poskytovateľa (issuer / discovery
URL), client ID a secret, a či ide o OIDC alebo SAML.

`AUTH_MODE=none` vypne prihlásenie úplne — použiteľné pri ladení v
lokálnej sieti, nikdy nie na verejnej adrese.

## Ukladanie

Stav aj história sa držia v pamäti a priebežne zapisujú do
`/data/state.json` (docker volume `relay-data`). Zápis ide cez dočasný
súbor a premenovanie, takže výpadok uprostred nezanechá poškodený JSON.

Na súčasný objem to stačí. Keby raz mala pribudnúť dlhá história alebo
grafy, patrí to do SQLite — nie do väčšieho JSON-u.
