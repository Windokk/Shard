# Système de plugins et de modules : état et roadmap restante

> Note interne, hors de `docs/docs/` (qui est la doc de l'utilisateur du moteur, en anglais). La référence de ce qui existe est `docs/docs/concepts/plugins.md`. Ce document garde les choix de conception, ce qui s'écarte du plan d'origine, et surtout **ce qui reste à faire**.
>
> Dernière mise à jour : 2026-10-22. Développé d'abord sous Linux ; depuis le 2026-10-20 la suite tourne sous **Windows (MinGW GCC)**, et la CI (`build.yml`) construit et teste sous Windows (MinGW GCC) et Linux (GCC). Rien sous macOS, et le moteur n'a jamais été compilé par MSVC (seuls des plugins de l'ABI C l'ont été).

## 1. Où on en est

Le design d'origine avait 9 phases. Bilan :

| Phase d'origine | État | Remarques |
|---|---|---|
| 0 · Désenregistrement par propriétaire | **fait** | `EventDispatcher` (tokens, owner), `ComponentRegistry`, `CVar` avec registre explicite, extensions de monde, `Scheduler::Remove` |
| 1 · Noyau (manifeste, résolveur, manager, `IPlugin`/`IModule`) | **fait** | `src/engine/plugins/`. Le `GameModule` est le module `game`, construit avec `shard_add_plugin(... ABI c1)` (`build/plugins/game/`) : depuis le 2026-10-22 il est écrit contre le SDK seul (`src/apps/game`, aucun include du moteur). Les anciennes bibliothèques (`InitializeSingletons`) chargent encore |
| 2 · Surface d'extension | **fait** | `PluginContext` et ses registrars, hooks, services, points d'extension |
| 3 · Modules et contenu | **fait** | `mod://`, couches, datapacks, patches de monde, composants déclarés (réels), composants inconnus conservés |
| 4 · Éditeur | **fait** | panneau Plugins, `editor.panel`, ImGui partagé avec les bibliothèques |
| 5 · Rechargement à chaud | **fait** | passe par `OnPrepareReload`/`OnRestore` et la mise de côté des mondes |
| 6 · Serveur, réseau, replays | **fait** (handshake) | pas de transport de jeu, voir §5 |
| 7 · ABI C stable et SDK public | **fait** (ABI 1.5) | voir R2 et `c-abi-sdk.fr.md` §13. Deux chemins : `abi "c1"` (`shard_plugin_entry`, table `ShardHostApi`, sans lier `Engine`, empreinte réduite à OS + arch) et `abi "cpp"` (`shard_plugin_describe`, `Engine` lié, empreinte complète) |
| 8 · Sandbox (WASM) et permissions appliquées | **commencé** (2026-10-23) | voir R6 et `wasm-sandbox.fr.md`. Un tier 2 est un module WebAssembly (`abi "wasm1"`) exécuté par Wasmtime, avec budgets ; une permission appliquée (`fs.read:content`) |
| 9 · Distribution | **fait** (hors TLS et hors UI) | registres, signatures, archives, `ShardPkg` |

Ajouts qui n'étaient pas dans le plan d'origine : budget mémoire compté, `ShardAssetValidator` comme hôte d'outils, `--check-plugins`, safe mode après un crash, `plugins.lock`, `SHARD_JOB_THREADS`, et, avec l'ABI C, une couche de gameplay complète dans le SDK (scripts, acteurs, input, physique, audio, mondes, panneaux d'éditeur).

## 2. Architecture telle que construite

### 2.1 Les pièces

```
src/engine/plugins/
  version, manifest, resolver      semver, `shard-plugin.json`, ordre de chargement stable
  plugin.hpp                       IPlugin / EnginePlugin / EditorPlugin / ServerPlugin / ToolPlugin / IModule, SHARD_PLUGIN
  context                          PluginContext + registrars, PluginMemory, tout est retiré en bloc au désenregistrement
  hooks, services, extensions      interception, interfaces entre plugins, points d'extension nommés
  content                          mod://, couches, tables, traductions, patches, validation des datapacks
  declared_component               composants-champs des datapacks (descripteur de réflexion bâti à l'exécution)
  manager, manager_runtime         découverte, chargement, cycle de vie, rechargement, lockfile, console
  archive, crypto, package         ZIP (inflate maison), SHA-2 + Ed25519 maison, signatures, magasin de confiance
  registry                         index, client HTTP/1.0, installateur
  handshake                        serveur/client TCP, offre de datapacks
  fingerprint                      chaîne de build comparée à celle de la bibliothèque (chemin cpp seulement)
  abi/                             ABI C : ShardHostApi, CAbiPlugin, une c_*.cpp par interface (c_internal.hpp les relie)
src/engine/c_abi_physics.cpp, c_abi_audio.cpp
                                   shard.physics et shard.audio, à la racine du moteur (voir ci-dessous)
sdk/                               en-têtes C (l'ABI), SDK C++ header-only, paquet CMake, gabarit
```

Le manager est à part dans la pile : rang 3.5 dans `scripts/check_deps.py` (au-dessus du monde, sous le renderer, la physique et l'audio). Le renderer l'importe pour lire `render.feature` / `render.pipeline`. Pour la même raison, `plugins/abi` ne peut pas inclure la physique ni l'audio : leurs tables (et `DecodeContact`, qui traduit les contacts Jolt pour les scripts) sont définies à la racine du moteur et seulement déclarées dans `c_internal.hpp`.

### 2.2 Cycle de vie

`Startup` (avant le moteur) : découverte → confiance → résolution → bibliothèques → `OnLoad` de tous → `OnInitialize` de tous → montage du contenu. Puis `OnEngineSystemsReady` (systèmes et abonnements en attente rejoignent le scheduler), `OnEngineInitialized` (`OnEnable`) juste avant le monde de départ, `BeginFrame`/`EndFrame`, `OnEngineDestroying` (tout est rendu avant que le scheduler ne soit détruit), `Shutdown` (bibliothèques).

Désactiver = tout retirer, garder la bibliothèque ; activer = relancer `OnLoad`/`OnInitialize`/`OnEnable` sur le même objet. Un plugin qui échoue entraîne ses dépendants. Un système qui lève 10 exceptions éteint son plugin.

**Désactivation et mondes** : tant qu'un monde est chargé, `Disable` refuse un plugin qui a enregistré des composants, ainsi que celui dont un tel plugin dépend (vérifié avant d'arrêter quoi que ce soit, donc pas d'arrêt partiel). Les instances vivent dans les mondes et leur vtable dans la bibliothèque ; seul `Reload` met les mondes de côté (`m_WorldsAside` lève l'interdiction pendant qu'il le fait). Un module sans composants reste désactivable à chaud.

### 2.3 Choix qui s'écartent du plan d'origine

| Plan d'origine | Réalisé | Pourquoi |
|---|---|---|
| Hash d'un plugin = id + version + binaire + contenu | id + version + **contenu** | le code machine n'est pas le même sous Windows et Linux ; un replay ou un serveur doivent pouvoir comparer d'une plateforme à l'autre. Le code est identifié par sa version |
| Singletons passés par une struct `PluginHostLinks` à terme remplacée par un contexte passé en paramètre | `HostLinks` + `LinkHostGlobals` + des registres qui n'exposent que des pointeurs vers les objets de l'hôte | `GetEngine()` n'est pas supprimé (phase 2 du tableau d'architecture, pas faite). Le `PluginContext` ne touche aucun global du plugin ; ce qui vit dans un `static` (extensions de monde, filtres de JSON de monde) passe par des `std::function` fournies par l'hôte |
| Notifier un plugin désactivé sans le décharger | idem | désactiver retire tout, donc `OnLoad` est relancé à l'activation |
| Archives `.shardplugin` compressées | écriture « stored », lecture stored + deflate | pas de dépendance zlib sous Windows ; la lecture couvre tout ce que produisent les outils usuels |
| Sandbox WASM (phase 8) | d'abord exclu, repris le 2026-10-23 sur Wasmtime | une fois l'ABI C faite, la sandbox réutilise ses interfaces ; un interpréteur maison a été écrit puis retiré le même jour : maintenir le moteur WebAssembly, c'est porter la sécurité de la sandbox (`wasm-sandbox.fr.md` §4) |
| `render.feature` & co. laissés aux sous-systèmes | lus par le renderer, le moteur (assets) et l'éditeur | sinon le point d'extension n'était qu'un nom |
| `https://` | refusé | rien ne parle TLS dans le moteur ; l'index et les archives sont protégés par hash et signature |

### 2.4 Confiance

Trois niveaux de vérification, dans cet ordre : signature **invalide** → toujours refusé ; plugin **installé depuis un registre** (marqueur `.shard-install.json`) → signature valide d'un éditeur de confiance obligatoire ; plugin **natif tier 1** → idem, ou `allowUnsigned` du projet, ou `--allow-unsigned-plugins`. Le reste (dossier posé à la main) charge sans signature. Le tier est **auto-déclaré** : un plugin malveillant peut se déclarer tier 0 pour échapper à la règle du tier 1, c'est pourquoi la règle « installé » repose sur un marqueur posé par l'installateur et pas sur le manifeste. Un dossier posé à la main n'est pas protégé : c'est un choix de l'utilisateur, pas une faille.

La signature couvre `shard-plugin-v1 \n id \n version \n digest`, où `digest` = SHA-512 de la liste triée `(chemin relatif, SHA-256 du fichier)`. Changer un fichier, l'id ou la version l'invalide. S n'est accepté que canonique (< L).

### 2.5 Ce que le moteur lit

| Point | Lu par | Item |
|---|---|---|
| `render.feature`, `render.pipeline` | `Renderer` | `RenderFeature`, `RenderPipelineExtension` |
| `asset.type`, `asset.importer` | `EngineInstance` | `AssetTypeExtension`, `AssetImporterExtension` |
| `editor.panel` | éditeur | `EditorPanelItem` |
| `tool.validator` | `ShardAssetValidator` | `ToolValidatorItem` |

Le renderer retire une feature ou un pipeline **sur le thread graphique, avant** que la bibliothèque ne parte : un objet dont la vtable est dans une bibliothèque déchargée est le piège le plus coûteux de tout le système.

## 3. Pièges : traités et ouverts

| Piège | État |
|---|---|
| Singletons dupliqués (moteur lié en statique dans chaque bibliothèque) | **réglé pour l'ABI C** (le plugin ne lie pas `Engine`). **Contourné** sur le chemin cpp : pointeurs vers l'hôte, `std::function` pour les registres globaux. Reste une dette tant que `GetEngine()` existe |
| Pointeurs vers du code déchargé | **traité** pour tout ce qui passe par le contexte (registres, événements, services, hooks, extensions, systèmes, cvars, et pour l'ABI C les types de scripts, marqués morts au déchargement). **Ouvert** : ce qu'un plugin enregistre par un autre chemin (thread, ressource GPU, handle propre) reste à sa charge ; un test sous ASan/TSan manque |
| `typeid` entre binaires | **traité** pour hooks et extensions (noms, pas `typeid`) et pour les événements de l'ABI C (canal nommé de l'`EventDispatcher`, ouvert aussi au C++). **Ouvert** : le canal typé indexe encore par `typeid` ; OK avec GCC sous Linux et Windows, jamais essayé avec un moteur MSVC |
| Allocation à travers la frontière | **traité pour l'ABI C** (`alloc`/`free` de l'hôte, comptés ; ce qui change de côté passe par là). **Partiel** sur le chemin cpp : `ctx.Memory()` passe par l'allocateur de l'hôte, mais rien n'oblige à l'utiliser |
| Données de mods perdues à la sauvegarde | **traité** : composants inconnus conservés. **Ouvert** : l'éditeur ne les affiche nulle part |
| Sécurité d'un plugin natif | **non résolu par construction** : signatures + consentement, aucune isolation |
| Ordre de chargement = ordre de simulation | **traité** : tri stable par id, `--check-plugins` |
| Empreinte de build incomplète | **sans objet pour l'ABI C** (OS + arch seulement, la compatibilité est celle des en-têtes, figée par `abi_layout_tests`). **Partiel** sur le chemin cpp : OS, arch, compilateur (majeur), build type, double précision, déterminisme. Pas la STL ni `_ITERATOR_DEBUG_LEVEL` |
| Rechargement et disposition des données | **traité** par la réflexion/JSON pour l'état du plugin (`shard.state` pour l'ABI C) ; les mondes sont mis de côté et rechargés depuis le disque (refus s'ils ont des modifications non sauvegardées) |
| Fichiers verrouillés sous Windows | **traité** (copie temporaire) ; les tests de rechargement de `dynamic_plugin_tests` passent sous Windows (MinGW) |
| Crash attribué au bon plugin | **partiel** : le plugin « actif » est noté (callbacks et systèmes du chemin cpp ; pour l'ABI C, chaque appel de l'hôte vers le plugin passe par un `ActiveScope`), pas l'adresse fautive |
| Handles d'acteur de l'ABI C et adresses réutilisées | **traité** (2026-10-22) : quand l'adresse d'un acteur détruit est reprise par un autre, l'ancienne case est libérée tout de suite (`Release` dans `c_actors.cpp`) au lieu d'attendre le balayage, et l'index par adresse n'est effacé que s'il désigne encore cette case |

## 4. Ce qui n'a pas été testé

* **macOS** : rien n'y a tourné.
* **Windows** : la suite passe avec MinGW GCC (machine de dev et CI). Pas de moteur MSVC : restent à risque MSVC pour `offsetof` sur le composant déclaré, les `#pragma GCC` et le canal d'événements typé (`typeid`). `fs::permissions` sur la clé secrète ne pose pas d'ACL.
* **L'éditeur en vrai** : il tourne avec le module `game` chargé en plugin (mode jeu, Game View). Le panneau Plugins, `editor.panel`, les panneaux de l'ABI C (`shard.editor`, testés avec un faux backend) et le bouton de rechargement n'ont pas été vérifiés à l'écran.
* **Le player** : il charge le module `game` ; `--hot-reload-plugins` et le contrôle des plugins au démarrage d'un replay ne sont couverts que par le chemin `ShardHeadless`.
* **Un vrai jeu avec des plugins tiers** : le seul plugin qui n'est pas de test est `game`, écrit contre le SDK mais dans le dépôt. Le gabarit est compilé hors du dépôt par `Sdk.OutOfTree`.

## 5. Roadmap restante

Chaque phase vise quelque chose de testable seul. Les durées sont des ordres de grandeur pour une personne.

### R0 · Vérifier ce qui existe (1 à 2 semaines), à faire en premier

* ~~Windows MinGW en CI~~ : fait, `build.yml` construit et teste Windows (MinGW GCC) et Linux (GCC), avec le replay de déterminisme et les étapes « plugin compilé par un autre compilateur » de l'ABI C (Clang sous Linux, MSVC sous Windows) ; tout passe en CI (2026-10-23). Reste : **macOS**, et un moteur compilé par **MSVC** (les CMake lient avec des options GNU, `-lz`).
* Faire tourner `ShardTests` sous **ASan et TSan** (le workflow TSan existe déjà, vérifier qu'il couvre `tests/plugins/`). Cibles : désenregistrement dans un handler, `PluginMemory`, `HookRegistry` pendant un `Emit`, rechargement d'une bibliothèque qui a des abonnements.
* Un test d'**éditeur lancé pour de vrai** (xvfb ou fenêtre masquée) qui charge le plugin d'exemple et ouvre un panneau.
* Fuzzer de formats (`ZipArchive`, `ParseManifest`, handshake, `ApplyWorldPatch`, `ValidateContentFolder`) : ce sont les frontières avec du contenu venu de l'extérieur. (Le chargement d'un plugin C et les tables de l'ABI ont déjà un fuzz léger, `c-abi-sdk.fr.md` A5.)
* **Sortie** : CI verte sur trois OS, aucun rapport de sanitizer.

### R1 · Brancher l'interface (1 à 2 semaines)

* Le panneau Plugins sait : chercher et installer dans un registre, mettre à jour, désinstaller, faire confiance à un éditeur (avec empreinte de clé affichée et confirmation), voir la signature d'un plugin, créer `plugins.lock`.
* Afficher les composants conservés d'un plugin absent (nom, JSON brut, « plugin manquant : id »).
* Un écran de démarrage du player qui, devant un serveur de handshake, propose de télécharger les datapacks manquants puis redémarre (`PluginHandshakeClient` existe, personne ne l'appelle côté player).
* **Sortie** : un utilisateur installe un plugin signé sans ouvrir de fichier JSON.

### R2 · ABI C stable et SDK public : fait (2026-10-21/22)

Plan, avancement, écarts et reste : `c-abi-sdk.fr.md` (§13). En bref :

* `ShardHostApi` + `query_interface`, ABI **1.5** (`sdk/VERSION` 1.5.0) : plugin, contenu, composants, ECS, systèmes, événements, cvars, commandes, hooks, services, extensions, état, scripts, acteurs, input, physique, audio, mondes, éditeur ; `shard.experimental.world_ecs` sans promesse.
* SDK C++ header-only (`sdk/include/shard/cpp/`, namespace `Shard`, `shard/game.hpp`), paquet CMake (`shard_sdk_add_plugin`), gabarit multi-fichiers, archive `shard-sdk-<version>.zip`, tutoriel `docs/docs/tutorials/write-a-game-with-the-sdk.md`, référence générée `docs/docs/api_reference.md`.
* Prouvé : plugin compilé hors du dépôt contre le SDK installé (`Sdk.OutOfTree`), plugin MSVC chargé par le moteur MinGW sous Windows et plugin Clang chargé par le moteur GCC sous Linux, en CI, plugin 1.0 chargé par le moteur courant (`AbiCompatTest`).
* Décision prise : le chemin cpp (`Engine` lié) est gardé pour les modules du moteur ; le module `game` est passé à l'ABI C.
* **Reste** : le critère de réception (module Voxel contre le SDK seul, R8), le coût par appel, le tutoriel sur une machine vierge, voir `c-abi-sdk.fr.md`.

### R3 · Assets des modules dans la base d'assets (2 à 3 semaines)

Aujourd'hui les fichiers d'un module sont lus par `mod://` et ne reçoivent pas d'`AssetID` entier de la base du projet ; un monde ne peut pas référencer une texture de module comme une texture du projet.

* Attribuer des `AssetID` aux fichiers montés (la GUID déterministe existe : `ContentMounts::AssetGuid`), ou ouvrir le VFS aux chemins `mod://` pour que `ResourcesManager::Get` les trouve.
* Charger un monde fourni par un datapack (aujourd'hui seulement patché, jamais chargé depuis `mod://.../worlds/`).
* Rechargement à chaud des fichiers de contenu de module (`HotReloader` ne les surveille pas).
* **Sortie** : un datapack ajoute une texture et un monde qui s'utilisent depuis l'éditeur comme les assets du projet.

### R4 · Cooker et paks (3 à 4 semaines, dépend du tableau d'architecture, phase 7)

Il n'y a ni cooker, ni baker, ni pak dans le dépôt (`src/apps/tools/{cooker,baker}` sont vides).

* Le cooker emballe les modules et datapacks dans des paks que le VFS monte ; `plugins.lock` et les signatures s'appliquent aux paks.
* Les outils hors-ligne (`ShaderC`, cooker, baker) chargent les plugins `tool` ; seul `ShardAssetValidator` le fait aujourd'hui.
* **Sortie** : un player lance un jeu cuit sans fichier libre, plugins compris.

### R5 · Réseau de jeu (au besoin, avec la phase « Networking » du tableau)

Le handshake est prêt, il n'y a pas de transport de jeu (UDP fiable, réplication par réflexion). Quand il existera :

* Brancher `PluginHandshakeServer` sur la connexion, pas à côté ; refuser en cours de partie un client dont un plugin change.
* Lister les plugins `simulation.affects` dans l'en-tête de session et dans le message de reconnexion.
* **Sortie** : un client et un serveur avec des plugins différents ne peuvent pas jouer ensemble, et le message dit lequel.

### R6 · Sandbox (tier 2) : commencée (2026-10-23)

Plan et avancement : `wasm-sandbox.fr.md`. Fait : le runtime sur Wasmtime 50 (C API précompilé téléchargé par CMake, carburant, mémoire bornée, NaN canoniques), le chargement d'un tier 2 `abi "wasm1"` comme un plugin C (sans signature), les budgets par image, les imports `log`, `cvars`, `commands`, `content.read` (permission `fs.read:content`), et un vrai plugin C compilé par Clang et lié par `wasm-ld` qui tourne dans la sandbox. Reste : les autres interfaces (générées depuis les en-têtes), leurs permissions, le SDK C++ en wasm, l'étalonnage du budget et la mesure contre le natif.

### R7 · Durcissement (au fil de l'eau)

* **TLS** pour le client de registre (ou un proxy documenté). Aujourd'hui `https://` est refusé ; l'index n'est pas signé, seul son contenu l'est : un serveur compromis peut **masquer** une mise à jour ou servir une ancienne version signée (rollback). Signer l'index (clé de registre) et refuser un index plus ancien que le dernier vu.
* Rotation et révocation de clés distribuée (aujourd'hui une liste locale `revoked`).
* Vérifier la signature à chaque démarrage sans relire tous les fichiers (cache de digest invalidé par date+taille), pour les gros plugins.
* Attribuer un crash à un plugin par l'adresse (plage des modules chargés) plutôt que par le dernier plugin « actif ».
* Compléter l'empreinte de build (STL, `_ITERATOR_DEBUG_LEVEL`).
* `ctx.Memory()` obligatoire pour le contenu lourd : fournir un `operator new` de remplacement actif dans la bibliothèque (opt-in CMake) pour compter sans effort.

### R8 · Modules de genre (phase 9 du tableau)

Premier vrai client du système : Voxel (après le noyau ECS et le snapshot de rendu), puis Space et Racing. Règle d'acceptation du tableau : « builds with zero engine-internal includes ». C'est le test de réception de R2, pas une phase indépendante.

## 6. Ordre et dépendances

```
R0 ──► R1
R2 (fait) ──► R8
          └──► R6 (commencée)
R3 ──► R4
R5 attend le transport réseau
R7 au fil de l'eau, TLS/index signé avant d'ouvrir un registre public
```

À faire d'abord : finir **R0** (macOS, moteur MSVC, sanitizers). Windows et Linux sont couverts par la CI.

## 7. Questions ouvertes

| Question | Pistes |
|---|---|
| Garder la liaison statique de `Engine` dans les plugins ? | Elle impose le même compilateur et les mêmes options. R2 l'a retirée pour les tiers et pour le module `game` ; elle reste pour les plugins `abi "cpp"` (modules du moteur, accès complet). Une bibliothèque partagée `ShardEngine` supprimerait les singletons dupliqués de ce chemin mais demande un gros travail de visibilité des symboles |
| `GetEngine()` | Le tableau d'architecture prévoit de le supprimer (phase 2). Quand ce sera fait, `HostLinks` devient inutile |
| Un registre « officiel » | Qui possède la clé ? Comment on la fait tourner ? À trancher avant R7 |
| Politique du tier 1 par défaut | Aujourd'hui : refusé sans signature ni consentement. Pour un moteur utilisé en local par des développeurs, c'est peut-être trop strict ; l'option `--allow-unsigned-plugins` existe |
| Plusieurs versions d'un même plugin | Non, une version par id ; à reconsidérer si deux mods ont besoin de deux majeures d'une même bibliothèque |

## 8. Suites de tests

| Fichier | Ce qu'il couvre |
|---|---|
| `tests/plugins/version_tests`, `manifest_tests`, `resolver_tests` | semver, manifeste, ordre, conflits, cycles |
| `registries_tests`, `context_tests`, `manager_tests` | hooks, services, extensions, désenregistrement, cycle de vie, verrou, safe mode |
| `content_tests`, `declared_component_tests`, `unknown_component_tests` | couches, tables, patches, composants déclarés, composants conservés |
| `archive_tests`, `crypto_tests`, `distribution_tests` | ZIP, SHA-2, Ed25519 (vecteurs RFC 8032), signatures, registres, HTTP, installation |
| `handshake_tests`, `memory_tests`, `extension_point_unit_tests`, `editor_imgui_tests` | handshake et téléchargement, budget mémoire, types d'assets / importers / pipelines, ImGui |
| `dynamic_plugin_tests` + `plugin_fixtures/` | vraies bibliothèques : chargement, déchargement, rechargement, empreinte, refus |
| `Plugins.ExtensionPoints`, `Plugins.CheckPlugins`, `Plugins.ServerHandshake`, `Tools.AssetValidator*` | de bout en bout avec `ShardHeadless` et les outils |
| `abi_layout_tests`, `abi_compat_tests`, `c_plugin_tests`, `c_abi_interfaces_tests` + fixtures `c_plugin`, `c_consumer`, `abi_1_0` | ABI C : dispositions figées, compatibilité 1.0, cycle de vie, chaque interface, fuzz des handles |
| `sdk_spin_tests`, `game_scripts_tests` + fixtures `sdk_spin`, `game_scripts` | SDK C++ : composants, systèmes, scripts, acteurs, physique, audio, mondes, panneaux, composants typés |
| `ExampleGameTest.*`, `Sdk.OutOfTree` (`Sdk.OutOfTreeMsvc`), `Docs.ApiReference`, `Determinism.Replay` | le module `game` chargé en vrai, gabarit compilé hors du dépôt, référence d'API à jour, replay avec le module de jeu |
