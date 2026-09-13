# Connexion Binance Spot Testnet

La connexion fournit les données de marché par WebSocket et l'envoi, la consultation
et l'annulation d'ordres sur **Binance Spot Testnet**. Les hôtes Testnet sont fixes ;
le programme ne propose pas de mode de trading avec fonds réels.

Le connecteur Python est un processus séparé. Il gère HTTPS/TLS, HMAC et WebSocket ;
le moteur C++ conserve ses dépendances et son chemin de matching actuels. Le flux
peut alimenter le nouveau `MarketDepth` C++, qui représente les niveaux agrégés de
Binance. Le carnet FIFO `Orderbook` continue de représenter les ordres de la simulation
locale. Aucune stratégie ne soumet automatiquement d'ordre en réaction au flux.

## Installation

Python **3.11 ou plus récent** est nécessaire. Depuis la racine du dépôt :

```bash
python3 -m venv build/binance-venv
build/binance-venv/bin/python -m pip install -r connectors/binance/requirements.txt
cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release \
  -DPython3_EXECUTABLE="$PWD/build/binance-venv/bin/python"
cmake --build build/release -j2
ctest --test-dir build/release --output-on-failure
```

Sur Debian/Ubuntu, si `venv` indique que `ensurepip` manque, installer le paquet
`python3-venv` correspondant à la version de Python. Les commandes REST utilisent
uniquement la bibliothèque standard ; `websockets` est nécessaire au flux continu.
Le consommateur C++ utilise `poll`/`read` et se compile sous Linux/Unix.

## Données publiques, sans clé

```bash
build/binance-venv/bin/python -m connectors.binance ping
build/binance-venv/bin/python -m connectors.binance info BTCUSDT
build/binance-venv/bin/python -m connectors.binance depth BTCUSDT

# JSON ligne par ligne : métadonnées, snapshots, invalidations.
build/binance-venv/bin/python -m connectors.binance feed BTCUSDT --count 10

# Transmission directe au processus C++, avec affichage de son meilleur bid/ask.
build/binance-venv/bin/python -m connectors.binance feed BTCUSDT \
  --engine build/release/binance_marketdata
```

Le flux souscrit à `btcusdt@depth20@100ms`. Chaque message remplace les 20 meilleurs
niveaux de chaque côté ; les sauts d'identifiants sont donc acceptables. Ce n'est pas
un flux différentiel, et aucun snapshot REST supplémentaire n'est requis pour le
synchroniser. Une reconnexion recommence avec un snapshot complet. Les identifiants
peuvent repartir plus bas après une nouvelle session, notamment lors des resets Testnet.

Les métadonnées viennent de `exchangeInfo`. Les décimales restent exactes :
`prix = price_ticks × tickSize`, `quantité = quantity_lots × stepSize`.
Par exemple, avec un tick de `0.01`, `7677400` ticks valent `76774.00` unités de cotation.
Les quantités sont en lots, pas en unités de cotation. Les données ne sont jamais
converties en flottants pour l'envoi d'ordres ni pour l'alimentation du C++.

Le connecteur répond aux pings via la bibliothèque WebSocket. Une coupure, un message
invalide, un arrêt serveur ou cinq secondes sans message déclenchent une invalidation
et une reconnexion avec délai croissant, plafonné à environ 30 secondes.
`--retries 10` borne les tentatives consécutives ; `--retries 0` arrête au premier échec.
Une session saine de plus de 30 secondes réinitialise le compteur.

Côté C++, `MarketDepth::Fresh()` expire après trois secondes sans snapshot reçu.
`BestBid()`/`BestAsk()` renvoient alors `std::nullopt`. Le consommateur affiche aussi
un événement `stale`, puis redevient disponible au snapshot valide suivant.
Il invalide à la fermeture du flux. Un flux Testnet peu actif peut donc être marqué
périmé même si sa connexion est encore ouverte.

## Clés et ordres Testnet

Créer une clé **HMAC Spot Testnet** depuis [le site Binance Testnet](https://testnet.binance.vision/)
et activer les permissions de trading nécessaires. Configurer les deux variables
dans le terminal local, sans écrire les valeurs dans le dépôt ou dans une commande
conservée dans l'historique. Exemple Bash avec saisie masquée :

```bash
read -r -s -p 'Clé API Testnet : ' BINANCE_TESTNET_API_KEY
export BINANCE_TESTNET_API_KEY
read -r -s -p 'Secret API Testnet : ' BINANCE_TESTNET_API_SECRET
export BINANCE_TESTNET_API_SECRET
```

Les clés sont lues uniquement depuis `BINANCE_TESTNET_API_KEY` et
`BINANCE_TESTNET_API_SECRET`. Elles ne sont pas transmises au processus C++.
Aucune clé n'est nécessaire pour les données publiques. Les clés RSA/Ed25519 ne sont
pas prises en charge par ce connecteur.

Exemples à adapter aux filtres et aux prix courants du Testnet :

```bash
# Vérifier les filtres actuels du symbole.
build/binance-venv/bin/python -m connectors.binance info BTCUSDT

# Validation Binance uniquement : cet endpoint ne crée aucun ordre.
build/binance-venv/bin/python -m connectors.binance validate BTCUSDT BUY LIMIT 0.001 \
  --price 60000.00 --client-id validation-001

# Créer réellement un ordre sur le TESTNET avec ses fonds de simulation.
build/binance-venv/bin/python -m connectors.binance place BTCUSDT BUY LIMIT 0.001 \
  --price 60000.00 --tif GTC --client-id essai-001

# Consulter son état, puis annuler s'il est encore ouvert.
build/binance-venv/bin/python -m connectors.binance query BTCUSDT essai-001
build/binance-venv/bin/python -m connectors.binance cancel BTCUSDT essai-001
build/binance-venv/bin/python -m connectors.binance open-orders BTCUSDT

# Exemple d'ordre au marché en quantité de l'actif de base.
build/binance-venv/bin/python -m connectors.binance place BTCUSDT BUY MARKET 0.001 \
  --client-id essai-marche-001
```

LIMIT accepte GTC, IOC ou FOK. MARKET ne prend ni prix ni politique temporelle.
La quantité est toujours celle de l'actif de base ; `quoteOrderQty` n'est pas exposé.
Les limites min/max, le tick, le pas de quantité et le notionnel des ordres limites
sont vérifiés localement après lecture de `exchangeInfo`. Binance reste l'autorité
pour les filtres dynamiques, le notionnel des ordres au marché et les contraintes du
compte. Un ancien exemple de prix peut être rejeté par ces filtres.

Chaque ordre exige un `--client-id` à conserver. Utiliser un nouvel identifiant pour
chaque nouvel ordre logique ; ne pas réutiliser celui d'un ordre terminé. Le client
ne répète **jamais** automatiquement POST/DELETE. En cas de timeout, de réponse
illisible ou d'erreur serveur ambiguë, l'exécution peut avoir eu lieu. Consulter
l'identifiant avec `query` avant de décider de la suite. Un résultat immédiatement
« introuvable » ne prouve pas que la première demande ne sera jamais exécutée.

Les requêtes privées sont signées HMAC-SHA256 après encodage des paramètres.
Le client synchronise l'horloge via `/api/v3/time` et utilise un `recvWindow` de 5 s.
Une application réutilisant longtemps l'instance Python doit rappeler `sync_time()`
périodiquement. HTTPS vérifie le certificat et le nom d'hôte ; les redirections ne
sont pas suivies. Le client réutilise sa connexion HTTPS tant qu'elle reste disponible.
Une instance appartient à un seul thread.

Un HTTP 429/418 renvoie le délai `Retry-After` et bloque les requêtes suivantes de
la même instance jusqu'à son expiration. Respecter aussi ce délai avant de relancer
la CLI : le blocage est en mémoire, pas partagé entre processus. Les erreurs exposent
le code HTTP/Binance et le caractère ambigu de l'exécution, sans recopier le corps
serveur ou les secrets. Les réponses d'ordres réussis sont imprimées en JSON.

## Utilisation depuis une application

Le client Python s'importe avec `from connectors.binance.client import Client`.
`Client.from_environment()` propose `rules`, `place`, `query`, `cancel`, `sync_time`
et `close`. Le carnet de marché C++ s'inclut avec `market_depth.hpp`.

Le protocole privé du pipe Python → C++ est ASCII, une trame par ligne :

```text
META BTCUSDT 0.01000000 0.00001000
STALE
SNAPSHOT 42 1 1 10000 10 10001 20
STALE
```

`SNAPSHOT sequence nbids nasks` est suivi des paires `price_ticks quantity_lots`,
bids décroissants puis asks croissants. Au plus 20 niveaux par côté. Le C++ valide
l'intégralité d'une trame avant son application. Une trame incorrecte invalide le
carnet et termine le consommateur avec un code non nul.

`MarketDepth::Apply` utilise des tableaux fixes et n'alloue pas. Le décodage JSON,
le protocole textuel et l'affichage du programme de démonstration allouent. Cette
connexion Python/WebSocket à 100 ms n'est pas une passerelle HFT native à latence
microseconde ; aucun gain de latence réseau n'est revendiqué. La fraîcheur est
mesurée à la réception locale, pas à l'horodatage du moteur de Binance.

## Validation et périmètre

Les tests couvrent HMAC, encodage, horloge, décimales exactes, filtres, erreurs réseau,
absence de répétition des mutations, limitation de débit, snapshots, reconnexion et
invalidations. Un serveur WebSocket local vérifie ping/pong et fragmentation. Les
tests de processus vérifient le pipe, les trames invalides, l'expiration et EOF.
Les tests WebSocket sont ignorés si la dépendance optionnelle n'est pas installée.

Le 13 septembre 2026, un ping HTTPS public et trois snapshots BTCUSDT du Testnet ont
été reçus avec succès jusqu'au processus C++. Aucun ordre privé n'a été envoyé lors
de cette vérification ; la validation authentifiée nécessite les clés de l'utilisateur.

Le suivi des ordres se fait par requêtes `query`/`open-orders`. Cette version ne
contient pas de User Data Stream privé, de journal durable, de stratégie automatique,
de trading Futures ni de mode production. Une connexion au marché ne remplace pas
la gestion des positions, du risque et de la reprise après incident.

## Références

- [REST Spot Testnet officiel : endpoints, signatures et erreurs](https://github.com/binance/binance-spot-api-docs/blob/master/testnet/rest-api.md)
- [WebSocket Spot Testnet officiel : partial book depth et heartbeat](https://github.com/binance/binance-spot-api-docs/blob/master/testnet/web-socket-streams.md)
- [Filtres Binance officiels](https://github.com/binance/binance-spot-api-docs/blob/master/testnet/filters.md)
- [Client asyncio websockets](https://websockets.readthedocs.io/en/stable/reference/asyncio/client.html)
