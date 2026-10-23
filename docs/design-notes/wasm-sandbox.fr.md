# Sandbox WebAssembly (tier 2) : plan

> Note interne, suite de `plugin-system.fr.md` (R6, phase 8 du plan d'origine) et de `c-abi-sdk.fr.md` (l'ABI C dont la sandbox reprend les interfaces). État au 2026-10-23 : W0, W1b (le runtime est Wasmtime, l'interpréteur maison de W1 est retiré) et W3 faits ; W2, W4 et W5 avancés (budgets par image, permission `fs.read:content`, un vrai plugin C compilé par Clang et lié par `wasm-ld` tourne dans la sandbox). Le §9 dit ce qui est fait, les écarts et ce qui reste. Le reste est le plan.
>
> Rédigé le 2026-10-23.

## 1. Pourquoi

Un plugin natif, même de l'ABI C, tourne dans le processus du moteur : il lit et écrit n'importe quelle mémoire, ouvre des fichiers et des sockets, boucle à l'infini, plante le moteur. La seule défense est en amont (signature, consentement, `plugins.lock`). C'est acceptable pour le code du jeu et pour un mod qu'on a choisi, pas pour un mod téléchargé depuis un registre public ou envoyé par un serveur (`PluginHandshakeServer` sait déjà proposer des datapacks, pas du code).

Le tier 2 (`TrustTier::Sandboxed`) est prévu depuis le début dans le manifeste et refusé aujourd'hui (`manager.cpp` : « sandboxed plugins (tier 2) are not supported by this engine build »). Objectif : qu'un plugin tier 2, compilé en WebAssembly contre le SDK, charge **sans signature ni consentement**, parce qu'il ne peut rien faire que l'hôte ne lui donne :

* pas de mémoire en dehors de la sienne (mémoire linéaire bornée) ;
* pas de fichier, de réseau, d'horloge ou d'aléa système ; seulement les interfaces de l'ABI C, filtrées par les **permissions** du manifeste ;
* un **budget CPU** (carburant compté par instruction) et un **budget mémoire** par instance ; le dépasser arrête le plugin, pas le moteur ;
* un piège (division par zéro, accès hors bornes, `unreachable`) arrête le plugin, pas le moteur ;
* une exécution **déterministe**, bit pour bit sur toutes les plateformes : un tier 2 peut avoir `simulation.affects` dans un replay ou une partie en réseau.

## 2. Périmètre

### Dans la v1

| Domaine | Ce que le plugin peut faire |
|---|---|
| Cycle de vie | les rappels de `ShardPluginCallbacks` (`on_load` … `on_unload`, `on_frame_begin/end`) |
| Interfaces | celles de l'ABI C 1.5 qui ne donnent pas accès à l'hôte : `shard.plugin`, `shard.content` (lecture, dans le dossier du plugin), `shard.components`, `shard.ecs`, `shard.systems`, `shard.events`, `shard.cvars`, `shard.commands`, `shard.state`, `shard.scripts`, `shard.actors`, `shard.input`, `shard.physics`, `shard.audio`, `shard.worlds` |
| SDK | le SDK C++ (`shard/game.hpp`) compilé pour `wasm32`, sans exceptions |
| Outils | `shard_sdk_add_plugin(... TARGET wasm)` produit un `.wasm` et le manifeste ; `ShardPkg` l'emballe comme un natif |

### Hors v1

| Domaine | Raison |
|---|---|
| `shard.editor` (panneaux) | demande des rappels ImGui à chaque image et un budget différent ; à reprendre après la v1 |
| `shard.services` entre un tier 2 et un natif | une table de pointeurs de fonctions natifs ne peut pas être appelée depuis la sandbox (et inversement) ; il faudra un service « par message » |
| `shard.hooks`, `shard.extensions` (validateurs, importeurs, types d'assets) | ils tournent dans les outils et le chargement d'assets, hors du budget par image ; pas de besoin en v1 |
| `shard.experimental.*` | rien d'expérimental dans un code non fiable |
| Fils (threads WebAssembly), SIMD, `memory64`, GC, exceptions WebAssembly | pas dans le MVP de WebAssembly que l'interpréteur implémente (§4) |
| JIT | voir §4 et W7 |

## 3. Principes

1. **Rien de l'hôte n'est visible.** La sandbox ne voit que sa mémoire linéaire et ses imports. Un pointeur de la sandbox est un décalage `uint32_t` dans cette mémoire, vérifié à chaque passage (`offset + size <= memory.size`), jamais converti en adresse de l'hôte avant vérification.
2. **Les handles de l'ABI C sont déjà des entiers** (`ShardEntity`, `ShardActor`, `ShardCVar`…) : ils traversent sans traduction. C'est ce qui rend l'ABI C réutilisable ici.
3. **Les structs sont recopiées, pas partagées.** En `wasm32` un pointeur fait 4 octets : `ShardFieldDesc`, `ShardComponentDesc`, `ShardSystemDesc`… n'ont pas la même disposition que sur l'hôte. Chaque import lit la struct de la sandbox champ par champ (disposition `wasm32`, figée par un test comme `abi_layout_tests`) et construit la struct de l'hôte. `ShardStr` (`{ptr32, pad, u64}`) a la même taille et le même décalage de `size` dans les deux, mais `data` doit quand même être traduit.
4. **Les rappels sont des index de table.** Un pointeur de fonction en `wasm32` est un index dans la table `funcref` du module. L'hôte appelle `call_indirect` sur cet index, vérifie la signature, et passe `user` tel quel (c'est un décalage qui ne veut rien dire pour l'hôte).
5. **Un plugin tier 2 n'est jamais appelé en parallèle.** Une instance WebAssembly n'a qu'une pile ; ses systèmes sont forcés `mainThread` (ou au moins sérialisés entre eux). Le parallélisme viendra, si besoin, de plusieurs instances.
6. **Déterminisme** : l'interpréteur canonicalise les NaN produits par l'arithmétique flottante (la spec laisse leur bit de signe et leur charge libres), n'expose aucune horloge, et le carburant est compté de la même façon partout. Un tier 2 qui affecte la simulation est comparé par version + contenu, comme un natif (le `.wasm` fait partie du contenu, donc du hash, ce qui est en plus vrai ici : le binaire est le même sur toutes les plateformes).
7. **Échec propre** : un piège, un carburant épuisé, une mémoire refusée ou un import mal utilisé arrêtent l'appel en cours, journalisent la cause attribuée au plugin et comptent comme une erreur (le dixième fait échouer le plugin, comme un système qui lève). L'état de l'instance après un piège n'est plus fiable : le plugin est désactivé, pas seulement l'appel annulé.

## 4. Décision : quel moteur WebAssembly

**Décision révisée le 2026-10-23 : Wasmtime.** Le premier choix (un interpréteur maison, fait en W1) est abandonné : maintenir un interpréteur ou un compilateur WebAssembly, c'est porter la sécurité de la sandbox, ce qui est trop de responsabilité pour ce projet. Le runtime doit être tiers, open source, et réduire l'écart avec le natif.

| Option | Pour | Contre |
|---|---|---|
| **Wasmtime** (Bytecode Alliance, Cranelift) — **retenu** | compile le wasm en code machine (JIT, ou AOT avec un module précompilé `.cwasm`) : l'écart avec le natif est de l'ordre de 1,2 à 2 fois, à mesurer en W6 ; fuzzé en continu et doté d'un processus de sécurité ; C API stable (`wasm.h` + `wasmtime.h`) ; archives précompilées du C API pour chaque plateforme, **MinGW compris** (`wasmtime-vX-x86_64-mingw-c-api.zip`, et `x86_64-linux`, `x86_64-windows` pour MSVC), donc pas de chaîne Rust à installer ; carburant déterministe (`consume_fuel`), interruption par époque (moins chère, non déterministe), canonicalisation des NaN (`cranelift_nan_canonicalization`), plafonds de mémoire et de table (limiteur du store) | grosse bibliothèque binaire (statique ou DLL) ; du code machine généré à l'exécution (mémoire exécutable) : un bug de Cranelift peut être une évasion, c'est le risque qu'on délègue ; le carburant a un coût (l'époque est moins chère mais seulement pour les plugins qui n'affectent pas la simulation) |
| WAMR (AOT par `wamrc`, LLVM) | proche du natif en AOT, en C, petit | le comptage d'instructions et la canonicalisation des NaN en AOT ne sont pas documentés clairement (pas trouvé) ; peu essayé sous MinGW |
| Wasmer (LLVM ou Cranelift) | LLVM est le plus rapide | C API moins centrale pour le projet, pas d'archive MinGW connue |
| Interpréteur maison (W1, abandonné) | aucune dépendance, tout maîtrisé | 10 à 50 fois plus lent que le natif ; la sécurité de la sandbox repose sur ce projet |

L'interface étroite de W1 (`Wasm::Module`, `Wasm::Instance`, `Call`, `CallIndirect`, `MemoryAt`, carburant, imports par nom avec `HostTrap`) reste : elle est réimplémentée sur Wasmtime (étape **W1b**, §8), et `wasm_plugin.cpp` ne change presque pas. Les tests `WasmRuntime.*` restent comme tests de cette interface.

## 5. Côté hôte

```
src/engine/plugins/wasm/
  wasm_runtime.hpp/.cpp     Module (décodage, contrôle des corps) et Instance (mémoire, globales, table, pile ; Call avec carburant ; pièges)
  wasm_plugin.hpp/.cpp      la sandbox d'un plugin : imports "shard", trampolines vers ses callbacks, puis CreateCPlugin
```

(Le découpage prévu en quatre fichiers, `wasm_module` / `wasm_interp` / `wasm_imports` / `wasm_plugin`, a été réduit à deux ; les imports générés iront dans un fichier à part en W2.)

* Le module importe des fonctions plates, une par fonction de table : `shard.components.register_type`, `shard.ecs.query_begin`… plutôt qu'une table de pointeurs (une sandbox ne peut pas déréférencer un pointeur de l'hôte). `query_interface` reste, mais ne rend qu'un booléen « offert » + la version : le SDK l'utilise pour savoir quelles fonctions il peut appeler.
* Chaque import fait : vérifier les décalages, lire la struct `wasm32`, appeler la fonction de l'ABI C de l'hôte (`GetCHostApi()` et ses tables, exactement le code des natifs), réécrire les sorties dans la mémoire de la sandbox. **Une seule implémentation des règles**, comme pour l'ABI C.
* Les imports sont générés à partir des en-têtes C par un script (comme `gen_api_reference.py`) : la liste des fonctions, leurs paramètres et la disposition `wasm32` des structs sont dans les en-têtes, l'écrire à la main pour quelques centaines de fonctions serait une source de fautes.
* Les données des composants ECS : un système de la sandbox lit et écrit des blocs d'archétype. L'hôte copie le bloc dans la mémoire de la sandbox avant l'appel et le recopie après (seulement les types déclarés en `writes`). C'est le coût principal ; mesuré en W6.
* Mémoire : `memory.grow` est refusé au-delà de `budgets.memoryMB` du manifeste (défaut 64 Mo pour un tier 2). Les blobs restent comptés dans le même budget.
* Carburant : `budgets.cpuMs` du manifeste devient un nombre d'instructions par image (étalonné une fois : 1 ms ≈ N instructions de l'interpréteur sur la machine de référence, la valeur étant fixe pour rester déterministe). Épuisé au milieu d'un appel : piège `OutOfFuel`.

## 6. Manifeste et chargement

| Champ | Valeur |
|---|---|
| `tier` | `2` (`TrustTier::Sandboxed`) |
| `abi` | `"wasm1"` |
| `binaries` | `{"wasm": "plugin.wasm"}` : une seule entrée, la même pour toutes les plateformes |
| `permissions` | appliquées : `content.read`, `input`, `audio`, `physics`, `worlds.load`, `actors.spawn`… ; un import d'une interface non permise répond `SHARD_E_UNSUPPORTED` (le module charge quand même, comme un natif sans `shard.editor`) |
| `budgets` | `cpuMs` (par image), `memoryMB` |

* `PluginManager::LoadLibrary` gagne une branche `wasm1` : lire le fichier, `Wasm::Module::Decode`, valider, instancier avec les imports, appeler l'export `shard_plugin_entry` (même signature que le natif, décalages au lieu de pointeurs). Pas de `DynLib`, pas de copie temporaire, pas d'empreinte.
* La règle de confiance (`plugin-system.fr.md` §2.4) ne demande ni signature ni consentement à un tier 2. Un tier 2 **signé** n'a pas plus de droits : la signature dit qui l'a écrit, pas ce qu'il peut faire.
* Le rechargement à chaud est plus simple qu'en natif : jeter l'instance, en créer une, `shard.state` entre les deux.

## 7. SDK

* Compilation : Clang (`--target=wasm32`, `-nostdlib` pour le C ; wasi-sdk pour le C++, sa libc et sa libc++, les imports WASI étant refusés sauf ceux que l'hôte bouche : `fd_write` vers le journal, `proc_exit` vers un piège, `random_get` refusé). `wasm-ld` avec `--export=shard_plugin_entry`, `--export-table`, `--import-undefined`.
* `shard_sdk_add_plugin(... TARGET wasm)` choisit la chaîne `wasm32` (variable `SHARD_WASM_TOOLCHAIN` ou wasi-sdk trouvé dans le `PATH`), écrit `abi "wasm1"` et `tier 2` dans le manifeste.
* Le SDK C++ lève `Shard::Error`. En `wasm32` sans exceptions, `SHARD_SDK_NO_EXCEPTIONS` transforme chaque `throw` en un appel à `shard.plugin.fail(message)`, qui piège : même effet vu de l'hôte (l'appel échoue, l'erreur est comptée).
* Un `shard_wasm.h` déclare les imports (`__attribute__((import_module("shard"), import_name("components.register_type")))`) ; les en-têtes C publics restent les mêmes, une couche `shard_wasm_tables.c` reconstruit les tables (`ShardComponentsApi`…) à partir des imports, pour que le même code source compile en natif et en wasm.

## 8. Étapes

Durées indicatives pour un agent, environ 8 à 12 heures au total. Chaque étape finit sur quelque chose qui se teste seul.

### W0 · Décisions et note (fait, 2026-10-23)

Cette note ; choix de l'interpréteur maison (§4).

### W1 · Runtime : décodage, validation, interpréteur (fait puis abandonné, voir §4)

* Décodeur des sections du MVP (type, import, function, table, memory, global, export, start, element, code, data, data count), LEB128 bornés, refus des tailles incohérentes.
* Validation du code (pile de types, blocs, sauts) : un module validé ne peut pas désynchroniser la pile de l'interpréteur.
* Interpréteur : entiers et flottants 32/64 bits, conversions (avec pièges sur les conversions hors bornes), mémoire (`load`/`store` toutes tailles, `memory.size/grow`, `memory.fill/copy`), globales, `call`, `call_indirect` avec vérification de signature, blocs, `br_table`. Carburant par instruction, profondeur d'appel bornée, NaN canonicalisés.
* Imports de fonctions de l'hôte (`std::function` sur une vue de la mémoire) ; exports par nom.
* **Sortie** : `WasmRuntimeTest.*` avec des modules écrits octet par octet dans le test (pas d'outil externe), pièges, carburant, mémoire bornée, déterminisme des NaN, fuzz léger du décodeur.

### W1b · Passer à Wasmtime (fait, 2026-10-23)

* Dépendance : l'archive C API précompilée de Wasmtime, version figée et téléchargée par CMake avec son SHA-256 (`FetchContent` avec `URL_HASH`), une par plateforme (`x86_64-mingw`, `x86_64-linux`, `x86_64-windows`). Pas de submodule : il demanderait Rust pour construire. Liaison statique de préférence (un seul exécutable à distribuer), sinon la DLL copiée à côté.
* `wasm_runtime.cpp` réimplémenté sur `wasmtime.h` : `wasmtime_module_new` (la validation est celle de Wasmtime), `wasmtime_linker` pour les imports (`HostFunction` enveloppée dans un `wasmtime_func_callback_t`, `HostTrap` → `wasmtime_trap_new`), `wasmtime_context_set_fuel` / `get_fuel`, `wasmtime_store_limiter` pour la mémoire, `wasmtime_config_cranelift_nan_canonicalization_set`, `wasmtime_config_consume_fuel_set`, threads, SIMD relaxé et `memory64` désactivés, `max_wasm_stack` pour la profondeur d'appel. `CallIndirect` lit la table exportée (`__indirect_function_table`, ou celle que le module exporte) et vérifie le type de la fonction.
* Les traductions de pièges : `wasmtime_trap_code` → `TrapKind` (le carburant épuisé, les accès hors bornes, la division par zéro, la pile épuisée…).
* Un `Engine` Wasmtime par processus, un `Store` par sandbox (un store n'est pas partagé entre plugins).
* AOT plus tard : `wasmtime_module_serialize` au packaging (`ShardPkg`) pour ne pas compiler au chargement ; un `.cwasm` est lié à la version de Wasmtime et à la machine, donc gardé dans un cache local, jamais distribué comme source de vérité.
* Supprimé : le décodeur, la validation et l'interpréteur maison (environ 1 300 lignes).
* **Sortie** : `WasmRuntime.*` et `WasmPluginTest.*` passent sur Wasmtime, sous MinGW et Linux ; `Wasm.SdkSignatures` inchangé.

### W2 · Imports de l'ABI C (2 à 3 h)

* Script `scripts/gen_wasm_imports.py` : en-têtes C → table d'imports + traductions de structs `wasm32` ↔ hôte ; test qui vérifie que la sortie est à jour (comme `Docs.ApiReference`).
* Dispositions `wasm32` figées par un test.
* **Sortie** : un module de test appelle `log`, `cvars.create_float`, `components.register_type` et `actors.*` à travers la sandbox, avec des décalages hors bornes refusés.

### W3 · Chargement (1 h)

* `abi "wasm1"`, `binaries.wasm`, branche de `LoadLibrary`, `WasmPlugin : IModule`, tier 2 accepté sans signature.
* **Sortie** : `dynamic_plugin_tests` charge, désactive, réactive, recharge et décharge un `.wasm` ; `--check-plugins` le liste.

### W4 · Permissions et budgets (1 h)

* Permissions appliquées import par import ; carburant par image ; mémoire bornée par `budgets.memoryMB` ; piège → plugin désactivé, attribué.
* **Sortie** : un module qui boucle, un qui alloue sans fin, un qui lit hors bornes, un qui appelle une interface non permise : le moteur continue, le journal dit lequel et pourquoi.

### W5 · SDK et chaîne de compilation (1 à 2 h)

* `shard_wasm.h`, couche de tables, `SHARD_SDK_NO_EXCEPTIONS`, `TARGET wasm` dans `shard_sdk_add_plugin`, gabarit, étape CI (wasi-sdk téléchargé par la CI).
* **Sortie** : le gabarit compile en `.wasm` hors du dépôt et `ShardHeadless --check-plugins` le charge (`Sdk.OutOfTreeWasm`).

### W6 · Déterminisme, performance (1 h)

* Le replay de déterminisme avec un tier 2 `simulation.affects` : mêmes hachages sous Windows et Linux.
* Mesure : `Spin` avec 10 000 entités en wasm contre le même en natif ; noter le rapport, fixer le défaut de `cpuMs`.

### W7 · Plus tard

* Les modules précompilés (`.cwasm`) mis en cache au premier chargement, pour ne plus compiler au démarrage.
* `shard.editor` et des services par message.
* Réseau (R5) : un serveur peut envoyer un tier 2 au client sans lui demander de consentement, puisque le code ne peut rien faire hors de la sandbox.

## 9. Avancement

### Fait le 2026-10-23 : W1 (remplacé par W1b), W3, W5 pour le C, une partie de W2 et de W4

**W1b, runtime sur Wasmtime** (`src/engine/plugins/wasm/wasm_runtime.{hpp,cpp}`, `cmake/ShardWasmtime.cmake`).

* Dépendance : le C API précompilé de **Wasmtime 50.0.0**, téléchargé par CMake à la configuration (`FetchContent`, `URL_HASH SHA256`), une archive par plateforme (`x86_64-mingw`, `x86_64-windows` pour MSVC, `x86_64-linux`). Cible importée `ShardWasmtime`, liée en `PRIVATE` à `ShardEngine` ; seul `wasm_runtime.cpp` inclut `wasmtime.h`. Liaison **dynamique**, et non statique comme le prévoyait W1b : `libwasmtime.a` pèse 70 Mo et `ShardEngine` est une bibliothèque statique liée par chaque exécutable et chaque plugin `cpp` de test, alors que `wasmtime.dll` (36 Mo) n'est chargée qu'une fois. Sous Windows la DLL est copiée à côté des exécutables (`build/`, comme `SDL3.dll`) ; sous Linux le rpath de build pointe sur `libwasmtime.so`. Un player distribué devra embarquer la DLL. L'archive contient aussi une variante `min` (1 Mo) qui n'exécute que des modules précompilés : la piste pour un player qui ne compile rien (W7).
* Configuration (un `Engine` par processus) : carburant, canonicalisation des NaN de Cranelift, pas de threads, pas de SIMD relâché, pas de `memory64`, de multi-mémoire, de GC, de références de fonctions, d'exceptions, de stack switching ni de modèle de composants ; `max_wasm_stack` à 512 Kio. Un `Store` par instance, avec un limiteur : mémoire (`maxMemoryPages`), éléments de table, une instance, une table, une mémoire.
* La validation et la compilation sont celles de Wasmtime. L'hôte refuse en plus tout import qui n'est pas une fonction (mémoire, table, globale : la sandbox ne reçoit jamais ce qui n'est pas à elle) et vérifie lui-même que chaque import a un `HostImport` du même nom et du même type, pour un message clair.
* Le module doit **exporter sa mémoire** (`memory`) et **sa table** (`__indirect_function_table`, `--export-table` de `wasm-ld`) : l'hôte n'atteint pas ce qui n'est pas exporté. Pendant la fonction `start`, avant que l'instance existe, une fonction de l'hôte atteint la mémoire par l'appelant (`wasmtime_caller_export_get`).
* `CallIndirect` lit la table exportée et vérifie l'entrée nulle et le type avant d'appeler. Les pièges de Wasmtime deviennent des `TrapKind` (`wasmtime_trap_code`) ; un piège levé par une fonction de l'hôte remonte comme une erreur sans code, reconnue par un drapeau. Le journal reçoit la cause, pas la trace de pile de Wasmtime.
* Différence de sémantique trouvée par les tests : une conversion flottant vers entier hors bornes est un « integer overflow », comme le dit la spec (seul NaN est une « invalid conversion ») ; l'interpréteur maison et son test se trompaient.
* Supprimés : le décodeur, la validation et l'interpréteur maison (W1, environ 1 300 lignes, écrits et testés le même jour). L'interface (`Module`, `Instance`, `HostImport`, `HostTrap`, `CallIndirect`, `MemoryAt`, `GrowMemory`, carburant) est restée ; `wasm_plugin.cpp` n'a changé que d'une ligne.
* Tests : les `WasmRuntime.*` (16) passent sur Wasmtime sans changement de fond (le constructeur de modules exporte maintenant mémoire et table, le fuzz fait 1 000 modules au lieu de 4 000 : chacun est compilé par Cranelift).

**W3, chargement** (`src/engine/plugins/wasm/wasm_plugin.{hpp,cpp}`, `PluginManager::LoadSandbox`).

* Manifeste : `"abi": "wasm1"`, `"binaries": { "wasm": "plugin.wasm" }` (le même fichier pour toutes les plateformes, `BinaryForThisPlatform` le rend), `entry` par défaut `shard_plugin_entry`. Un `tier: 2` qui n'est pas `wasm1` est refusé avec un message qui le dit ; le message « not supported by this engine build » a disparu.
* **Choix** : un plugin wasm est un plugin C pour l'hôte. `CreateWasmPlugin` remplit un `ShardPluginInfo` natif dont les callbacks sont des trampolines qui appellent la sandbox (`CallIndirect` sur l'index de table que le module a donné), puis passe par `ValidateCPluginInfo` et `CreateCPlugin`. Cycle de vie, facettes (`GetCPluginFacets`), désenregistrement en bloc et attribution sont ceux des plugins C, sans une ligne de plus dans le manager.
* Le point d'entrée a la même forme que le natif : `shard_plugin_entry(host, info)`, avec `host` = 0 et `info` l'adresse d'une page que l'hôte ajoute à la mémoire du module (l'allocateur du module ne l'a jamais vue). L'hôte relit le `ShardPluginInfo` et le `ShardPluginCallbacks` dans leur disposition wasm32 (pointeurs de 4 octets ; `id` en 16, `user` en 32, `callbacks` en 36 ; 40 octets). Callbacks reliés : `on_load` … `on_unload`, `on_frame_begin/end`. Pas `on_prepare_reload` : un tier 2 n'est pas encore rechargeable à chaud.
* Rien n'est chargé dans le processus (`library` reste fermée), pas de copie temporaire, pas d'empreinte, pas de marqueur de safe mode. La règle de confiance existante ne demande déjà ni signature ni consentement à un tier 2.
* La sandbox d'un plugin vit dans une table par propriétaire, libérée quand sa bibliothèque le serait (`ReleaseWasmPlugin` dans le déchargement) et remplacée à la réactivation.

**W4, budgets** (sans les permissions).

* Carburant : `budgets.frameMs` × `kWasmFuelPerMs` (1 000 000 instructions par ms, valeur fixe et pas encore étalonnée, défaut 4 ms). **Par image** : ce qui tourne pendant une image (`on_frame_begin`, `on_frame_end`, et plus tard les systèmes et les scripts) partage ce budget, rendu à chaque `PluginManager::BeginFrame` (`BeginWasmFrame`). Les rappels de cycle de vie et les commandes, qui ne sont pas dans une image, ont chacun un budget entier. Mémoire : `budgets.memoryMB` (défaut 64 Mo).
* Un piège, un budget épuisé ou un import qui échoue : l'appel renvoie `SHARD_E_INTERNAL`, le journal du plugin dit pourquoi, et la sandbox est marquée morte (plus aucun rappel, `on_disable` / `on_unload` répondent OK). Le plugin passe en échec comme un plugin C dont un callback renvoie une erreur.
* Le journal dit si c'est le budget de l'image ou celui d'un appel qui a été dépassé.
* Tests : `WasmFrameTest.TheBudgetComesBackEveryFrame` (5 images à 60 % du budget : le plugin reste actif) et `WhatRunsDuringOneFrameSharesItsBudget` (deux rappels à 60 % chacun dans la même image : il échoue).
* **Permissions** : la première appliquée est `fs.read:content`, pour `shard.content.read` (lecture d'un fichier du dossier de contenu du plugin, copié dans un tampon de la sandbox). Sans elle l'import répond `SHARD_E_UNSUPPORTED` : le module charge quand même, comme prévu au §6. Le chemin est relatif au dossier du plugin : pas de `..`, rien d'absolu, pas d'adresse `mod://` d'un autre module, et `resolve` (qui donnerait un chemin de l'hôte) n'est pas offert. Tests : `ItReadsItsContentWithThePermission`, `WithoutThePermissionTheReadIsRefused`, `APathOutOfItsContentFolderIsRefused`.

**W2, imports** (écrits à la main, pas générés).

* `shard.log`, `shard.cvars.create_float/create_int/get_float/get_int/set_float/set_int`, `shard.commands.add` et `shard.content.read`. Le `ctx` que passe la sandbox est ignoré : chaque import agit pour le plugin de la sandbox. Sans ça, un module pourrait passer le `ctx` d'un autre plugin et agir en son nom. Les `ShardStr` passés par valeur arrivent par adresse (convention wasm32 de Clang) et sont lus avec vérification des bornes.
* Une commande console écrite dans la sandbox est le premier rappel de l'hôte hors du cycle de vie : ses mots sont copiés comme des `ShardStr` wasm32 dans une **page de travail** que l'hôte garde pour lui (celle qu'il a ajoutée pour le `ShardPluginInfo`), le callback tourne avec le budget du plugin, et le texte vers lequel il pointe `output` est recopié hors de la sandbox.
* Côté module : `sdk/include/shard/shard_wasm.h` déclare les imports (`__attribute__((import_module("shard"), import_name(...)))`), seulement quand on compile pour `__wasm__`. La fixture `tests/plugin_fixtures/wasm_plugin/wasm_plugin.c` (un vrai plugin C : cvar, commande) est compilée par Clang en wasm32 par le test `Wasm.SdkSignatures`, qui vérifie que les signatures produites (`shard_plugin_entry (i32, i32) -> (i32)`, `on_load (i32, i64) -> (i32)`, `shard_wasm_cvar_create_float (i64, i32, f32, i32, i32, f32, f32, i32) -> (i32)`…) sont celles que l'hôte relie. Les dispositions wasm32 de `ShardPluginInfo` (40 octets, `id` 16, `user` 32, `callbacks` 36), de `ShardStr` et de `ShardPluginCallbacks` ont été vérifiées avec Clang (`_Static_assert`) ; elles sont figées par des `_Static_assert` dans la fixture, donc par `Wasm.SdkSignatures` quand Clang est là. Le test n'existe que si `clang` est trouvé.
**W5 pour le C** : `wasm-ld` installé (`mingw-w64-x86_64-lld`). Quand CMake trouve `clang` et `wasm-ld`, la fixture C est compilée et liée en un vrai `.wasm` (1,5 Ko) dans `test_plugins/shard.test.wasm/`, avec son manifeste tier 2, la permission `fs.read:content` et un dossier de contenu. `WasmSdkPluginTest.APluginCompiledByClangRunsInTheSandbox` le charge avec le vrai `PluginManager` : la cvar est créée, `on_enable` lit `hello.txt` et l'incrémente, la commande console répond. La CI installe Clang et `lld` (MSYS2 et Ubuntu) ; ailleurs ces tests sont sautés. La commande de construction est dans `shard_wasm.h`.

* Tests : `WasmPluginTest.*` (9) : manifeste, chargement sans signature, désactivation et réactivation, budget épuisé, accès hors mémoire, tier 2 non wasm refusé, module tronqué, import inconnu (`wasi_snapshot_preview1.fd_write`) refusé avec son nom, cvars créées dans la sandbox, lues, modifiées et retirées avec le plugin, commande lancée depuis la console et retirée avec le plugin.

### Reste

* **W2** : le générateur d'imports à partir des en-têtes C (`scripts/gen_wasm_imports.py`) et les dispositions wasm32 figées par un test ; les interfaces restantes (composants, ECS, systèmes, événements, commandes, état, scripts, acteurs, input, physique, audio, mondes, contenu) ; `query_interface` côté sandbox ; les appels réentrants (un système ou un script appelé pendant qu'un import tourne).
* **W1b** : le cache des modules précompilés (`wasmtime_module_serialize`), pour ne pas compiler à chaque chargement ; vérifier la CI Linux (rpath de `libwasmtime.so`, jamais essayé ici).
* **W4** : les permissions des interfaces suivantes, à mesure qu'elles sont importées (`input`, `audio`, `physics`, `worlds.load`, `actors.spawn`…).
* **W5** : la couche qui reconstruit les tables (`ShardCVarsApi`…) à partir des imports pour que le même source compile en natif et en wasm ; `TARGET wasm` dans `shard_sdk_add_plugin` et le gabarit ; le SDK C++ en wasm (il lui faut une libc et une libc++ : wasi-sdk, et `SHARD_SDK_NO_EXCEPTIONS`).
* **W6** : étalonner `kWasmFuelPerMs` sur Wasmtime en Release, mesurer le rapport sandbox / natif sur `Spin`, le replay avec un tier 2 sous Windows et Linux.
* Le hachage d'identité d'un tier 2 : le `.wasm` est le même partout, il pourrait entrer dans le hachage (aujourd'hui seuls la version et le contenu y sont, comme pour un natif).

## 10. Risques

| Risque | Parade |
|---|---|
| Faille dans Wasmtime (Cranelift) = évasion de la sandbox | délégué à un projet fuzzé en continu, avec un processus de sécurité ; suivre ses avis de sécurité et monter de version (`cmake/ShardWasmtime.cmake`) |
| Faille dans les imports de l'hôte = évasion | chaque accès à la mémoire de la sandbox passe par `MemoryAt` (bornes vérifiées), le `ctx` de la sandbox est ignoré, revue de `wasm_plugin.cpp` comme d'un code de sécurité |
| Trop lent | budget par image ; mesure en W6 ; W7 |
| Disposition `wasm32` des structs mal recopiée | générée, et figée par un test, jamais écrite à la main |
| Le SDK C++ dépend des exceptions | `SHARD_SDK_NO_EXCEPTIONS` (W5), testé dans la fixture wasm |
| Chaîne wasm absente d'une machine | les tests de W1 à W4 construisent leurs modules octet par octet ; ceux de W5 sont sautés sans `clang` et `wasm-ld` |
| Archive Wasmtime indisponible ou modifiée | version figée, SHA-256 vérifié par CMake ; un miroir interne peut la servir en changeant l'URL |
