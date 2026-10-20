# Système de plugins et de modules : état et roadmap restante

> Note interne, hors de `docs/docs/` (qui est la doc de l'utilisateur du moteur, en anglais). La référence de ce qui existe est `docs/docs/concepts/plugins.md`. Ce document garde les choix de conception, ce qui s'écarte du plan d'origine, et surtout **ce qui reste à faire**.
>
> Dernière mise à jour : 2026-10-19. Tout a été développé et testé sous **Linux uniquement**.

## 1. Où on en est

Le design d'origine avait 9 phases. Bilan :

| Phase d'origine | État | Remarques |
|---|---|---|
| 0 · Désenregistrement par propriétaire | **fait** | `EventDispatcher` (tokens, owner), `ComponentRegistry`, `CVar` avec registre explicite, extensions de monde, `Scheduler::Remove` |
| 1 · Noyau (manifeste, résolveur, manager, `IPlugin`/`IModule`) | **fait** | `src/engine/plugins/`. Le `GameModule` est le module `game`, les anciennes bibliothèques (`InitializeSingletons`) chargent encore |
| 2 · Surface d'extension | **fait** | `PluginContext` et ses registrars, hooks, services, points d'extension |
| 3 · Modules et contenu | **fait** | `mod://`, couches, datapacks, patches de monde, composants déclarés (réels), composants inconnus conservés |
| 4 · Éditeur | **fait** | panneau Plugins, `editor.panel`, ImGui partagé avec les bibliothèques |
| 5 · Rechargement à chaud | **fait** | passe par `OnPrepareReload`/`OnRestore` et la mise de côté des mondes |
| 6 · Serveur, réseau, replays | **fait** (handshake) | pas de transport de jeu, voir §5 |
| 7 · ABI C stable et SDK public | **non fait** | voir R2. On a l'empreinte de build (`kBuildFingerprint`) et le descripteur C++ `shard_plugin_describe`, pas la table `ShardHostApi` |
| 8 · Sandbox (WASM) et permissions appliquées | **non fait, décision** | exclu volontairement ; le tier 2 est refusé avec un message |
| 9 · Distribution | **fait** (hors TLS et hors UI) | registres, signatures, archives, `ShardPkg` |

Ajouts qui n'étaient pas dans le plan d'origine : budget mémoire compté, `ShardAssetValidator` comme hôte d'outils, `--check-plugins`, safe mode après un crash, `plugins.lock`, `SHARD_JOB_THREADS`.

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
  fingerprint                      chaîne de build comparée à celle de la bibliothèque
```

Le manager est à part dans la pile : rang 3.5 dans `scripts/check_deps.py` (au-dessus du monde, sous le renderer, la physique et l'audio). Le renderer l'importe pour lire `render.feature` / `render.pipeline`.

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
| Sandbox WASM (phase 8) | refusé, pas implémenté | décision |
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
| Singletons dupliqués (moteur lié en statique dans chaque bibliothèque) | **contourné** : pointeurs vers l'hôte, `std::function` pour les registres globaux. Reste une dette tant que `GetEngine()` existe |
| Pointeurs vers du code déchargé | **traité** pour tout ce qui passe par le contexte (registres, événements, services, hooks, extensions, systèmes, cvars). **Ouvert** : ce qu'un plugin enregistre par un autre chemin (thread, ressource GPU, handle propre) reste à sa charge ; un test sous ASan/TSan manque |
| `typeid` entre binaires | **traité** pour hooks et extensions (noms, pas `typeid`). **Ouvert** : `EventDispatcher` indexe encore par `typeid` ; OK sous Linux (comparaison par nom), à vérifier sous MSVC |
| Allocation à travers la frontière | **partiel** : `ctx.Memory()` passe par l'allocateur de l'hôte, mais rien n'oblige à l'utiliser |
| Données de mods perdues à la sauvegarde | **traité** : composants inconnus conservés. **Ouvert** : l'éditeur ne les affiche nulle part |
| Sécurité d'un plugin natif | **non résolu par construction** : signatures + consentement, aucune isolation |
| Ordre de chargement = ordre de simulation | **traité** : tri stable par id, `--check-plugins` |
| Empreinte de build incomplète | **partiel** : OS, arch, compilateur (majeur), build type, double précision, déterminisme. Pas la STL ni `_ITERATOR_DEBUG_LEVEL` |
| Rechargement et disposition des données | **traité** par la réflexion/JSON pour l'état du plugin ; les mondes sont mis de côté et rechargés depuis le disque (refus s'ils ont des modifications non sauvegardées) |
| Fichiers verrouillés sous Windows | **traité en théorie** (copie temporaire), **jamais exécuté sous Windows** |
| Crash attribué au bon plugin | **partiel** : le plugin « actif » est noté (callbacks et systèmes), pas l'adresse fautive |

## 4. Ce qui n'a pas été testé

* **Windows et macOS** : aucun des tests ni des outils n'y a tourné. Points à risque : `DynLib`, la copie temporaire des bibliothèques, `fs::permissions` sur la clé secrète (ACL, pas POSIX), les ports du test de handshake, les chemins avec backslash dans les archives, MSVC pour `offsetof` sur le composant déclaré et les `#pragma GCC`.
* **L'éditeur en vrai** : compilé, jamais lancé. Panneau Plugins, `editor.panel`, `BindEditorImGui` et le bouton de rechargement sont vérifiés par des tests unitaires et par un plugin d'exemple, pas à l'écran.
* **Le player** : idem. `--hot-reload-plugins` et le contrôle des plugins au démarrage d'un replay ne sont couverts que par le chemin `ShardHeadless`.
* **Un vrai jeu avec des plugins tiers** : le seul plugin qui n'est pas de test est `game`.

## 5. Roadmap restante

Chaque phase vise quelque chose de testable seul. Les durées sont des ordres de grandeur pour une personne.

### R0 · Vérifier ce qui existe (1 à 2 semaines) — à faire en premier

* Faire tourner toute la suite sous **Windows (MSVC et MinGW)** et **macOS** ; corriger. Ajouter ces cibles à la CI (`build.yml`).
* Faire tourner `ShardTests` sous **ASan et TSan** (le workflow TSan existe déjà, vérifier qu'il couvre `tests/plugins/`). Cibles : désenregistrement dans un handler, `PluginMemory`, `HookRegistry` pendant un `Emit`, rechargement d'une bibliothèque qui a des abonnements.
* Un test d'**éditeur lancé pour de vrai** (xvfb ou fenêtre masquée) qui charge le plugin d'exemple et ouvre un panneau.
* Fuzzer de formats (`ZipArchive`, `ParseManifest`, handshake, `ApplyWorldPatch`, `ValidateContentFolder`) : ce sont les frontières avec du contenu venu de l'extérieur.
* **Sortie** : CI verte sur trois OS, aucun rapport de sanitizer.

### R1 · Brancher l'interface (1 à 2 semaines)

* Le panneau Plugins sait : chercher et installer dans un registre, mettre à jour, désinstaller, faire confiance à un éditeur (avec empreinte de clé affichée et confirmation), voir la signature d'un plugin, créer `plugins.lock`.
* Afficher les composants conservés d'un plugin absent (nom, JSON brut, « plugin manquant : id »).
* Un écran de démarrage du player qui, devant un serveur de handshake, propose de télécharger les datapacks manquants puis redémarre (`PluginHandshakeClient` existe, personne ne l'appelle côté player).
* **Sortie** : un utilisateur installe un plugin signé sans ouvrir de fichier JSON.

### R2 · ABI C stable et SDK public (3 à 4 semaines)

C'est la phase 7 du plan d'origine, non commencée. Plan détaillé : `c-abi-sdk.fr.md`. Aujourd'hui un plugin natif se lie à tout `Engine` (statique) : sa compatibilité repose sur l'empreinte de build, donc sur la même toolchain que le moteur.

* `ShardHostApi` : table de fonctions C versionnée (`structSize`, `apiMajor/Minor`, `QueryInterface(id, minVersion)`), allocation de l'hôte, journal.
* Interfaces d'extension en C (composants, systèmes, cvars, commandes, événements, services) et un SDK C++ header-only qui les enrobe ; les classes actuelles deviennent un adaptateur.
* Un plugin compilé **hors du dépôt**, sans lier `Engine`, chargé par un moteur d'une autre version mineure (test de compatibilité 1.0 → 1.1).
* Gabarit de projet de plugin (`CMake` + `shard-plugin.json`) et tutoriel (`docs/tutorials.md`).
* **Décision à prendre avant** : garder le tier 0 (C++, `Engine` lié) à côté, ou tout migrer. Recommandation : garder, il sert au code du jeu.
* **Sortie** : le module « Voxel » (phase 9 du tableau) s'écrit sans aucun include interne.

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

### R6 · Sandbox (tier 2) — exclu pour l'instant

Quand ce sera repris : WebAssembly (déterministe, carburant mesurable), ABI d'imports = les interfaces C de R2 (**dépend donc de R2**), permissions réellement appliquées (`fs.read:content`, `net`, `render.feature`), budget CPU et mémoire par instance. Ne rien commencer avant R2.

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
 │
 └──► R2 ──► R8
       │
       └──► R6 (si un jour)
R3 ──► R4
R5 attend le transport réseau
R7 au fil de l'eau, TLS/index signé avant d'ouvrir un registre public
```

À faire d'abord : **R0**. Tout le reste suppose que ce qui existe tient sur les trois systèmes.

## 7. Questions ouvertes

| Question | Pistes |
|---|---|
| Garder la liaison statique de `Engine` dans les plugins ? | Elle impose le même compilateur et les mêmes options. R2 la retire pour les tiers, pas pour le jeu. Une bibliothèque partagée `ShardEngine` supprimerait les singletons dupliqués mais demande un gros travail de visibilité des symboles |
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
