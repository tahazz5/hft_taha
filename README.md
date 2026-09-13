# Local matching engine — C++20

Moteur de carnet d'ordres mono-instrument, avec priorité prix/temps, tests et replay
textuel. Une connexion **Binance Spot Testnet** fournit les données WebSocket et
les ordres de simulation via REST : voir [le guide Binance](docs/BINANCE.md).
Le projet ne contient pas de stratégie automatique ni de gestion de positions.

## Compilation et utilisation

```bash
cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release
cmake --build build/release -j2
ctest --test-dir build/release --output-on-failure
./build/release/orderbook_benchmark
./build/release/orderbook_replay examples/orders.txt
```

Contrôles mémoire et comportement indéfini (GCC/Clang) :

```bash
cmake -S . -B build/sanitize -DCMAKE_BUILD_TYPE=Debug -DORDERBOOK_SANITIZERS=ON
cmake --build build/sanitize -j2
ctest --test-dir build/sanitize --output-on-failure
```

## API

Inclure `orderbook.hpp` et lier la cible CMake `orderbook`.
Les prix sont des ticks entiers et les quantités des unités entières `uint64_t`.
L'application fixe la taille du tick et du lot.

```cpp
Orderbook book(100000); // réservation de l'index des identifiants
book.AddOrder({1, 100, 10, side::ASK});
book.ExecuteOrder({2, 100, 4, side::BID});
auto remaining = book.FindOrder(1); // quantité restante : 6
```

- `AddOrder` insère directement dans le carnet, sans matching. Utile pour charger
  un état initial ; peut créer un carnet croisé.
- `ExecuteOrder` matche aux prix des ordres passifs, par priorité prix puis FIFO.
  GTC ajoute le reliquat ; IOC l'abandonne ; FOK rejette sans mutation si la
  liquidité disponible ne suffit pas. `true` signifie accepté, pas forcément exécuté.
- Un ordre au marché utilise `ExecuteOrder(o, TimeInForce::IOC, true)` ou FOK ;
  son prix est ignoré, son reliquat ne repose jamais dans le carnet.
- `ModifyOrder` modifie directement un ordre existant sans matching. Une réduction
  de quantité garde la priorité ; une augmentation ou un changement de prix/côté
  le place en fin de file. Pour soumettre un remplacement agressif, l'appelant doit
  explicitement annuler puis exécuter (deux opérations, sans atomicité de remplacement).
- `CancelOrder`, `FindOrder`, `Size`, `BestBid`, `BestAsk` et `Snapshot` donnent accès
  à l'état. Le snapshot est déterministe : bids décroissants, asks croissants, FIFO.
- Un meilleur ordre absent est représenté par `order{}` ; spread et mid-price sont
  NaN si un côté manque. Les identifiants nuls sont donc interdits.
- `SetTradeCallback` fournit maker, taker, prix, quantité et côté agressif à chaque
  exécution. Callback synchrone `noexcept`, sans réentrée dans le moteur.

Un identifiant doit être unique parmi les ordres actifs ; il peut être réutilisé
après annulation/exécution totale. Les ordres invalides et doublons sont rejetés
avant le matching. `Limits` borne la quantité par ordre, le notionnel des ordres
limites et le nombre d'ordres actifs. Le contrôle notionnel utilise une division
pour éviter un débordement. Les ordres au marché sont uniquement bornés en quantité :
ces limites ne constituent pas une gestion complète du risque financier.
À capacité maximale, GTC est rejeté avant toute exécution, même s'il pourrait matcher.
IOC/FOK restent acceptables sans capacité de stockage supplémentaire.

## Performances et architecture

Les niveaux utilisent des arbres de prix, les files FIFO des listes et l'index
un hash map. Un `std::pmr::unsynchronized_pool_resource` commun recycle les nœuds.
Les changements de priorité déplacent les nœuds avec `splice`, sans les réallouer.
Le matching ne fait aucun affichage ; le meilleur ordre ne copie pas la file.
L'annulation utilise les itérateurs mémorisés sans rechercher de nouveau le prix.

Le moteur est à propriétaire unique : aucun accès concurrent, y compris en lecture,
pendant les mutations. L'index est préréservé, mais le pool croît à la demande ;
il ne garantit ni zéro allocation ni une latence maximale. FOK parcourt d'abord la
liquidité puis exécute. Un snapshot alloue et parcourt tous les ordres.
Une allocation échouée pendant l'insertion d'un reliquat GTC peut survenir après
les premières exécutions : aucune garantie transactionnelle face à `bad_alloc`.

Le benchmark sépare insertion initiale, lecture du meilleur ask, modification,
annulation, insertion avec pool réutilisé, matching et balayage. Il vérifie les
retours, consomme les résultats et exclut les affichages des zones chronométrées.
Le cas « match partial » inclut périodiquement des exécutions totales de makers.

Exemple observé dans cet environnement (GCC 15.2, Release, 100 000 opérations,
100 niveaux de prix, 13 septembre 2026) :

| Opération | Moyenne d'un lot |
| --- | ---: |
| Insertion initiale | 105 ns/ordre |
| Insertion avec pool réutilisé | 72 ns/ordre |
| Augmentation de quantité | 14 ns/ordre |
| Annulation | 56 ns/ordre |
| Matching | 17 ns/ordre entrant |
| Balayage | 54 ns/ordre passif |

Ces mesures varient selon la charge et le matériel. Elles mesurent un débit en
cache chaud, pas une latence réseau ni des percentiles p99. Aucun facteur de gain
sur l'ancien code n'est revendiqué : ses tests ne compilaient pas et son annulation
contenait un accès à un itérateur invalidé.

## Replay

Le fichier accepte des lignes et des commentaires `#` :

```text
ADD 1 ASK 100 10
EXECUTE 2 BID 100 4 GTC
MODIFY 1 ASK 101 6
MARKET 3 BID 0 2 IOC
CANCEL 1
```

Format : `commande id côté prix quantité [GTC|IOC|FOK]`, ou `CANCEL id`.
Les politiques sont réservées à EXECUTE/MARKET. Le programme émet les événements
TRADE puis les ordres restants. Il s'arrête au premier rejet avec un numéro de ligne
et un code non nul ; les lignes précédentes restent exécutées. C'est un replay
séquentiel déterministe, sans horodatages ni journal de reprise durable.

## Tests et fichiers

Les vérifications restent actives en Release. La suite couvre les rejets, la FIFO,
les changements de côté, les fills, IOC/FOK/marché, les limites et les états vides.
Elle compare 30 000 opérations déterministes à un modèle vectoriel indépendant et
exécute un balayage de 100 000 ordres. CTest vérifie aussi un scénario de replay.

- `include/orderbook.hpp` : bibliothèque C++20.
- `src/replay.cpp` : outil de replay.
- `tests/orderbook_tests.cpp` : régressions, comparaison aléatoire et stress.
- `benchmarks/orderbook_benchmark.cpp` : microbenchmarks.
- `examples/orders.txt` : scénario exécutable.

Le fichier historique `order.cpp`, qui contenait des modifications locales, est
conservé intact et exclu des cibles CMake. Utiliser la nouvelle bibliothèque pour
bénéficier des corrections.

La connexion Binance Spot Testnet est décrite dans [docs/BINANCE.md](docs/BINANCE.md).
Les prochaines étapes pour une infrastructure de trading complète sont le
suivi privé en continu, le contrôle du risque par compte, le journal
durable et la reprise, puis les files interthreads et les mesures de latence de bout
en bout. Elles nécessitent de choisir une place de marché et un modèle de déploiement.
