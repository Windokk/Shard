# ABI C stable et SDK public : plan

> Note interne, suite de `plugin-system.fr.md` (phase R2, phase 7 du plan d'origine). État au 2026-10-22 : A0 à A5 sont faits, puis le reste du plan, puis les révisions 1.3 (gameplay), 1.4 (scripts, acteurs, input) et 1.5 (physique, audio, mondes, panneaux d'éditeur, champs déclarés dans la classe, composants typés) ; le module de jeu de l'exemple est écrit contre le SDK seul. L'ABI est en 1.5 (`sdk/VERSION` 1.5.0), le SDK C++ est dans le namespace `Shard`. Le §13 dit ce qui a été fait, les écarts au plan et ce qui reste. Les §1 à §12 sont le plan tel qu'il a été écrit avant le code (les noms de fichiers et d'interfaces y ont parfois changé, voir les écarts).
>
> Rédigé le 2026-10-20.

## 1. Pourquoi

Aujourd'hui un plugin natif est une bibliothèque C++ qui lie `Engine` en statique et exporte `shard_plugin_describe` (`plugin.hpp`). L'hôte lui donne des pointeurs (`HostLinks`), récupère un `IPlugin*` et appelle ses méthodes virtuelles. Tout ce qui traverse la frontière est du C++ : vtables, `std::string`, `std::function`, `std::shared_ptr`, `nlohmann::json`, `typeid`.

Conséquences :

* **Même toolchain obligatoire.** La seule garantie est l'empreinte `SHARD_BUILD_FINGERPRINT` (OS, arch, compilateur majeur, build type, physique). Un plugin compilé avec GCC 13 ne charge pas dans un moteur compilé avec GCC 14 ; un plugin MinGW ne charge pas dans un moteur MSVC ; un plugin debug ne charge pas dans un player release.
* **Même version du moteur, en pratique.** La disposition de n'importe quelle classe incluse peut changer d'un commit à l'autre sans que `kPluginApiMajor/Minor` ne bouge. L'empreinte ne le voit pas.
* **Pas de SDK.** Écrire un plugin demande le dépôt complet, ses includes internes et ses options CMake.
* **Singletons dupliqués.** Chaque bibliothèque porte sa copie des globals du moteur, d'où `LinkHostGlobals` et les `std::function` de `HostBindings`.

Objectif : qu'un tiers écrive un plugin avec **un dossier d'en-têtes et un fichier CMake**, sans lier `Engine`, et que ce binaire charge dans **toutes les versions mineures suivantes** du moteur, quel que soit le compilateur de l'un et de l'autre (à OS et architecture égaux).

## 2. Périmètre

### Dans la v1

| Domaine | Ce que le plugin peut faire |
|---|---|
| Cycle de vie | les mêmes étapes que `IPlugin` (`OnLoad` … `OnUnload`, `OnPrepareReload`/`OnRestore`), plus `OnFrameBegin/End` d'`EnginePlugin` et les rappels d'`IModule` |
| Base | journal, mémoire de l'hôte (`PluginMemory`), manifeste, chemins `mod://`, lecture de fichiers de contenu |
| Composants | déclarer un type de composant par un descripteur de champs (réflexion), lire et écrire ses champs |
| Systèmes | ajouter un système (phase, lectures, écritures, ordre), parcourir des entités, enregistrer des changements structurels dans le `CommandBuffer` |
| Événements | émettre et recevoir des événements **nommés** |
| CVars, commandes | les quatre types de cvars actuels, commandes de console |
| Hooks | les hooks de `hooks.hpp`, désignés par leur `kName` |
| Services | fournir et demander une interface par nom et version |
| Points d'extension | `tool.validator`, `asset.type`, `asset.importer` (les items qui n'ont que des données et des fonctions) |

### Hors v1 (et pourquoi)

| Domaine | Raison |
|---|---|
| `render.feature`, `render.pipeline` | les objets du render graph et du RHI ne sont pas stables. À reprendre quand le render graph sera gelé |
| `editor.panel` et tout ImGui | ImGui n'a pas d'ABI stable. Piste plus tard : exposer `cimgui` construit par l'hôte, ou une petite API de widgets propre |
| Acteurs et `World` en accès direct | l'arbre acteurs/composants doit passer par des handles ; on commence par ce que demande le module Voxel (§10) |
| Réseau, `ServerPlugin` | rien à exposer tant qu'il n'y a pas de transport (R5) |
| Liaisons Rust, Zig, C# | possibles grâce au C, mais pas livrées |

## 3. Principes de l'ABI

Ces règles valent pour **tout** en-tête de `sdk/include/shard/` ; elles sont vérifiées par la revue et, autant que possible, par des tests (§9).

1. **C pur.** Les en-têtes compilent en C11 et en C++17. Pas de classe, pas de référence, pas de surcharge, pas d'`enum class` (des `uint32_t` + `#define` ou `enum` avec une taille forcée).
2. **Types à taille fixe.** `int32_t`, `uint64_t`, `float`, `double`. Jamais `int`, `long`, `size_t` dans une struct (taille variable entre Windows et Linux pour `long`). `size_t` toléré en paramètre seulement.
3. **Booléens** : `uint8_t` dans les structs, `int32_t` en retour (`SHARD_TRUE`/`SHARD_FALSE`). Pas de `bool` C++ dans une struct partagée.
4. **Chaînes** : `ShardStr { const char* data; uint64_t size; }`, UTF-8, sans obligation de zéro final. Une chaîne reçue n'est valable que pendant l'appel ; la garder = la copier.
5. **Toute struct partagée commence par `uint32_t structSize`.** On n'ajoute des champs **qu'à la fin**. Celui qui lit vérifie `structSize` avant de lire un champ ajouté après la première version (le motif existe déjà dans `HostLinks::tags`).
6. **Pas de pointeur vers un objet de l'hôte, des handles.** `ShardEntity`, `ShardComponentType`, `ShardSystem`, `ShardToken` : des `uint64_t` (index + génération). Un handle périmé échoue proprement au lieu de lire de la mémoire libérée.
7. **Rappels** : un pointeur de fonction + un `void* user`. L'hôte ne fait jamais rien avec `user` à part le rendre.
8. **Propriété explicite.** Ce que l'hôte alloue, l'hôte le libère (et réciproquement). La mémoire qui change de côté passe par `host->mem.alloc/free`.
9. **Aucune exception ne traverse.** Côté hôte, chaque entrée de table est enveloppée (`try/catch` → `SHARD_E_INTERNAL`). Côté plugin, le SDK C++ fait pareil autour de chaque rappel. Pas de `longjmp` à travers la frontière.
10. **Erreurs** : `typedef int32_t ShardResult;` `0` = succès, négatif = erreur (`SHARD_E_INVALID_ARG`, `SHARD_E_NOT_FOUND`, `SHARD_E_ALREADY_EXISTS`, `SHARD_E_WRONG_THREAD`, `SHARD_E_STALE_HANDLE`, `SHARD_E_UNSUPPORTED`, `SHARD_E_OUT_OF_MEMORY`, `SHARD_E_INTERNAL`). Le message détaillé va dans le journal de l'hôte, attribué au plugin.
11. **Convention d'appel** : `SHARD_CALL` sur chaque pointeur de fonction (vide sur x86_64 et arm64, `__cdecl` réservé si un jour x86 32 bits). Export : `SHARD_PLUGIN_EXPORT` existant.
12. **Fils d'exécution** : chaque fonction de la table porte une étiquette dans sa doc : `any` (n'importe quel fil), `system` (appelable depuis un système, donc en parallèle), `main` (fil principal, hors systèmes). Un appel `main` depuis un système renvoie `SHARD_E_WRONG_THREAD` en debug.
13. **Rien de déterministe ne dépend de la toolchain du plugin** : l'ABI ne promet pas que deux compilateurs calculent les mêmes flottants. Les plugins `simulation.affects` restent comparés par version + contenu (§2.3 de `plugin-system.fr.md`), pas par binaire.

## 4. Point d'entrée et chargement

### 4.1 Ce que le plugin exporte

```c
#define SHARD_ABI_MAJOR 1
#define SHARD_ABI_MINOR 0

typedef struct ShardPluginInfo {
    uint32_t structSize;
    uint32_t abiMajor, abiMinor;          /* versions des en-têtes avec lesquels il a été compilé */
    ShardStr id;                          /* informatif, le manifeste fait foi */
    void*    user;                        /* rendu à chaque rappel de ShardPluginCallbacks */
    const struct ShardPluginCallbacks* callbacks;
} ShardPluginInfo;

/* Appelée une fois, juste après le chargement de la bibliothèque. */
SHARD_PLUGIN_EXPORT ShardResult SHARD_CALL
shard_plugin_entry(const struct ShardHostApi* host, ShardPluginInfo* out);
```

`ShardPluginCallbacks` reprend `IPlugin` : `on_load`, `on_initialize`, `on_enable`, `on_disable`, `on_unload`, `on_prepare_reload`, `on_restore`, puis (ajoutés par familles, tous facultatifs, `NULL` = rien) `on_frame_begin/end`, `on_content_mounted`, `on_world_loaded/unloaded`, `on_play_start/stop`, `on_tool_task`. Chacun reçoit `(void* user, ShardPluginCtx ctx)` ; `ShardPluginCtx` est un handle qui désigne le plugin auprès de l'hôte (son `PluginOwner`).

### 4.2 Ce qui change dans le manifeste

Un champ `abi` dans `shard-plugin.json` :

| Valeur | Point d'entrée cherché | Vérification |
|---|---|---|
| `"cpp"` (défaut, comportement actuel) | `entry`, défaut `shard_plugin_describe` | `kPluginApiMajor/Minor` + empreinte de build complète |
| `"c1"` | `entry`, défaut `shard_plugin_entry` | `abiMajor == SHARD_ABI_MAJOR`, `abiMinor <= SHARD_ABI_MINOR` de l'hôte ; empreinte réduite à **OS + arch** |

`PluginManager::LoadLibrary` lit `abi` et part dans l'une ou l'autre branche ; la branche C construit un `CAbiPlugin : IPlugin` (adaptateur, §6) qui sera traité ensuite comme n'importe quel plugin. Le reste du manager (résolution, confiance, désactivation, rechargement, budget) ne change pas.

Le tier ne change pas non plus : un plugin `c1` est un natif tier 1, signé ou consenti. L'ABI C n'isole rien.

### 4.3 Versions

* **Majeure** de l'ABI : cassure. Un hôte peut, s'il le veut, garder la table de la majeure précédente (pas prévu en v1).
* **Mineure** : ajout de fonctions **à la fin** des tables, ajout de champs **à la fin** des structs, nouvelles interfaces. Un plugin 1.0 charge dans un hôte 1.3 ; un plugin 1.3 est refusé par un hôte 1.0 (message : « construit pour l'ABI 1.3, ce moteur a 1.0 »).
* **Par interface** : chaque interface a sa propre version (§5.2). Un plugin demande `components >= 1.1` ; s'il l'a, il peut appeler ce qui a été ajouté en 1.1.
* Le numéro d'ABI est **indépendant** de la version du moteur et de `kPluginApiMajor/Minor` (qui reste celui du chemin C++).

## 5. La table de l'hôte

### 5.1 `ShardHostApi`

```c
typedef struct ShardHostApi {
    uint32_t structSize;
    uint32_t abiMajor, abiMinor;
    ShardStr engineVersion;                                   /* "0.9.2", informatif */

    /* Base : toujours là, fil "any" */
    void        (SHARD_CALL *log)(ShardPluginCtx, uint32_t level, ShardStr message);
    void*       (SHARD_CALL *alloc)(ShardPluginCtx, uint64_t size, uint64_t align);   /* compté dans budgets.memoryMB */
    void        (SHARD_CALL *free)(ShardPluginCtx, void* p);
    ShardResult (SHARD_CALL *query_interface)(ShardStr id, uint32_t major, uint32_t minMinor, const void** out);
} ShardHostApi;
```

Tout le reste passe par `query_interface` : la table de base ne grossit presque jamais, chaque domaine évolue à son rythme, et l'hôte peut refuser une interface (un outil hors-ligne n'offre pas `shard.systems`).

### 5.2 Les interfaces de la v1

| Id | Contenu | Fil | Correspond à |
|---|---|---|---|
| `shard.plugin` 1.0 | manifeste (id, version, dossier racine), chemin `mod://`, permissions demandées | any | `PluginContext::Manifest()` |
| `shard.content` 1.0 | ouvrir/lire un fichier de contenu, lister un dossier, résoudre `mod://` | any | `ContentMounts` |
| `shard.components` 1.0 | `register_type(desc)`, `find_type(name)`, `get_field`/`set_field` par nom ou par index | register : main ; champs : system | `ComponentRegistrar`, `DeclaredComponentType` |
| `shard.ecs` 1.0 | requêtes (`query_begin(reads, writes)` → itération par blocs d'archétype : pointeur + nombre + pas), `entity_valid`, commandes (`cmd_add`, `cmd_remove`, `cmd_destroy`, `cmd_spawn`) | system | `Registry`, `CommandBuffer` |
| `shard.systems` 1.0 | `add_system(desc)`, `set_enabled`, `remove` | main | `SystemRegistrar`, `SystemDesc` |
| `shard.events` 1.0 | `subscribe(name, cb)`, `subscribe_world`, `emit(name, payload, size)`, `unsubscribe(token)` | subscribe : main ; emit : any | `EventRegistrar` |
| `shard.cvars` 1.0 | créer bool/int/float/string avec bornes et drapeaux, lire, écrire, être prévenu d'un changement | create : main ; get : any | `CVarRegistrar` |
| `shard.commands` 1.0 | `add(name, help, cb)` ; le rappel reçoit les arguments en `ShardStr[]` | main | `CommandRegistrar` |
| `shard.hooks` 1.0 | `on(name, priority, cb)` ; le rappel reçoit une struct de hook C (`ShardHookPreWorldLoad`…) et peut annuler | main | `HookRegistrarApi` |
| `shard.services` 1.0 | `provide(name, major, minor, const void* vtable, void* self)`, `get(name, range)` → `ShardServiceRef` (handle, vide si le fournisseur part) | any | `ServiceRegistrar`, `ServiceRef` |
| `shard.extensions` 1.0 | `add(point, name, const void* item, item_type)` pour les points de la v1 ; items C décrits §5.5 | main | `AssetRegistrar` |
| `shard.state` 1.0 | écrire et relire l'état de rechargement en JSON texte | main | `StateWriter`/`StateReader` |

Tout ce qui est enregistré passe par le `PluginContext` du plugin : le **désenregistrement en bloc** (`ReleaseAll`) marche sans rien ajouter, c'est l'acquis le plus précieux de la phase 0.

### 5.3 Composants

On réutilise ce qui existe : `DeclaredComponentType` construit déjà un `ClassDescriptor` à l'exécution à partir d'une liste de champs, et `DeclaredFieldsComponent` sait sauver, charger, copier et s'afficher dans l'inspecteur. Le chemin C enregistre un composant de la même façon, avec une description venue d'une struct C au lieu d'un JSON de datapack :

```c
typedef struct ShardFieldDesc {
    uint32_t structSize;
    ShardStr name;
    uint32_t type;            /* SHARD_FIELD_F32, _I32, _BOOL, _VEC3, _QUAT, _STRING, _ASSET, _ENTITY, _TAG… */
    uint32_t offset;          /* dans le bloc de données du composant */
    uint32_t flags;           /* SHARD_FIELD_HIDDEN, _READONLY, _NO_SAVE */
    ShardStr defaultJson;     /* facultatif */
} ShardFieldDesc;

typedef struct ShardComponentDesc {
    uint32_t structSize;
    ShardStr name;            /* "voxel.Chunk" : préfixé par l'id du plugin */
    uint32_t size, align;     /* <= DeclaredComponentType::kMaxStorage aujourd'hui */
    const ShardFieldDesc* fields; uint32_t fieldCount;
    /* facultatifs : NULL = mémoire mise à zéro puis valeurs par défaut, copie octet par octet */
    void (SHARD_CALL *construct)(void* user, void* data);
    void (SHARD_CALL *destroy)(void* user, void* data);
    void (SHARD_CALL *copy)(void* user, void* to, const void* from);
    void* user;
} ShardComponentDesc;
```

À trancher pendant A2 : la limite de 1 Ko de `kMaxStorage` (assez pour un composant de gameplay, pas pour un chunk de voxels ; un champ `SHARD_FIELD_BLOB` alloué par l'hôte est la piste) et le lien entre ces composants d'acteur et les composants ECS de `core/ecs` (même nom = même `ComponentId` ? cf. §8).

### 5.4 Systèmes

`ShardSystemDesc` = `SystemDesc` à plat : `name`, `phase` (les 7 valeurs de `Ecs::Phase`), `reads`/`writes` en tableaux de `ShardComponentType`, `exclusive`, `mainThread`, `after`/`before` en tableaux de `ShardStr`, `run(user, const ShardSystemCtx*)`. `ShardSystemCtx` reprend `SystemContext` (`deltaTime`, `fixedDeltaTime`, `fixedStep`, `fixedStepCount`, `frame`) plus des handles sur le registre et le `CommandBuffer` du système. Le compteur de 10 exceptions qui éteint un plugin devient un compteur de 10 `ShardResult` d'erreur renvoyés par `run` (ou d'exceptions attrapées par l'adaptateur).

### 5.5 Événements, hooks, extensions : par nom

`EventDispatcher` et `ExtensionRegistry` s'appuient sur `typeid` : inutilisable depuis un binaire C. Côté C :

* **Événements** : un nom (`"voxel.ChunkBuilt"`) et une charge en octets ; facultativement un `ShardComponentDesc`-like pour que l'éditeur et les replays sachent l'afficher. L'hôte ajoute au `EventDispatcher` un canal par nom à côté du canal par type (utile aussi pour résoudre l'« ouvert » `typeid` sous MSVC du §3 de `plugin-system.fr.md`).
* **Hooks** : déjà par nom (`kName`). Une struct C par hook, générée à partir de `hooks.hpp` ou écrite à la main, avec `cancelled`/`reason` ; l'adaptateur copie dans les deux sens.
* **Extensions** : chaque point de la v1 publie un type d'item C (`ShardToolValidatorItem`, `ShardAssetTypeItem`, `ShardAssetImporterItem`) ; l'adaptateur l'enrobe dans l'item C++ que le sous-système lit déjà (`ToolValidatorItem`…). Les sous-systèmes ne changent pas.

### 5.6 Services

Un service C est une struct de pointeurs de fonctions avec `structSize` en tête, plus un `void* self`. Les deux plugins incluent le même en-tête de service (publié par le fournisseur, pas par le moteur). `ServiceRef` devient `ShardServiceRef` : un handle dont l'hôte remet le slot à `NULL` quand le fournisseur part, exactement comme `ServiceSlot` aujourd'hui. Le SDK C++ fournit `Shard::Service<T>` par-dessus.

## 6. Côté hôte : l'adaptateur

```
src/engine/plugins/abi/
  c_api.hpp/.cpp          construction de ShardHostApi, query_interface, enveloppe try/catch commune
  c_plugin.hpp/.cpp       CAbiPlugin : IPlugin / EnginePlugin / IModule qui appelle ShardPluginCallbacks
  c_components.cpp        ShardComponentDesc → DeclaredComponentType
  c_systems.cpp           ShardSystemDesc → SystemDesc, ShardSystemCtx
  c_events.cpp            canal nommé de l'EventDispatcher
  c_hooks.cpp             structs de hooks C ↔ Hooks::*
  c_services.cpp, c_extensions.cpp, c_cvars.cpp, c_content.cpp, c_state.cpp
  handles.hpp             tables de handles index+génération
```

* Chaque fonction de la table retrouve le `PluginContext` par son `ShardPluginCtx` et appelle le registrar existant : **une seule implémentation** des règles (noms préfixés, propriétaire, budget, désenregistrement), partagée par les deux ABI.
* `HostLinks`, `LinkHostGlobals` et l'empreinte complète ne concernent que le chemin `cpp`.
* Le manager note `m_ActivePlugin` autour de chaque rappel C, comme pour les systèmes C++ : l'attribution des plantages marche pareil.
* Rechargement à chaud : `on_prepare_reload` / `on_restore` + `shard.state` ; la mise de côté des mondes s'applique sans changement.

## 7. Le SDK

### 7.1 Contenu

```
sdk/
  include/shard/
    shard_abi.h             types de base, ShardStr, ShardResult, macros, versions
    shard_host.h            ShardHostApi, point d'entrée, ShardPluginCallbacks
    shard_components.h, shard_ecs.h, shard_systems.h, shard_events.h, shard_cvars.h,
    shard_commands.h, shard_hooks.h, shard_services.h, shard_extensions.h, shard_content.h, shard_state.h
    cpp/                    SDK C++ header-only (C++17)
      plugin.hpp            classe Shard::Plugin + macro SHARD_C_PLUGIN(Classe)
      component.hpp         SHARD_COMPONENT(T, champs…) qui produit le ShardComponentDesc
      system.hpp, events.hpp, cvars.hpp, services.hpp, hooks.hpp, string.hpp (string_view ↔ ShardStr)
  cmake/
    ShardSDKConfig.cmake    find_package(ShardSDK 1.0)
    ShardPlugin.cmake       shard_sdk_add_plugin(cible SOURCES … MANIFEST shard-plugin.json)
  templates/plugin/         CMakeLists.txt, shard-plugin.json (abi "c1"), src/plugin.cpp, README.md
  VERSION                   version du SDK = version de l'ABI (1.0.0)
```

* Les en-têtes `sdk/include/shard/*.h` **sont** l'ABI : rien dans `src/engine` ne les définit ailleurs, le moteur les inclut lui-même (l'adaptateur compile contre eux).
* Le SDK C++ ne contient que du code inline : il est recompilé avec le plugin, donc il peut utiliser la STL librement sans que cela touche l'ABI. Il attrape toutes les exceptions à la frontière.
* `shard_sdk_add_plugin` : bibliothèque partagée, visibilité cachée par défaut, seul le point d'entrée exporté, copie du manifeste, cible `package` qui appelle `ShardPkg`.
* Installation : `cmake --install` du moteur pose le SDK dans `<prefix>/sdk` ; la CI en fait aussi une archive (`shard-sdk-1.0.0.zip`).

### 7.2 À quoi ressemble un plugin

```cpp
#include <shard/cpp/plugin.hpp>
#include <shard/cpp/component.hpp>
#include <shard/cpp/system.hpp>

struct Spin { float speed = 1.f; float angle = 0.f; };
SHARD_COMPONENT(Spin, (speed)(angle));

class SpinPlugin : public Shard::Plugin {
public:
    void OnLoad(Shard::Context& ctx) override
    {
        auto spin = ctx.Components().Register<Spin>("spin.Spin");
        ctx.Systems().Add("spin.Rotate", Shard::Phase::Update)
            .Writes(spin)
            .Run([spin](Shard::SystemCtx& s) {
                s.Each<Spin>(spin, [&](Spin& c) { c.angle += c.speed * s.DeltaTime(); });
            });
        ctx.CVars().Float("spin.scale", 1.f, "multiplies every speed");
    }
};
SHARD_C_PLUGIN(SpinPlugin)
```

Aucun include de `src/engine`. Le même plugin s'écrit en C pur avec les tables, plus verbeux.

### 7.3 Documentation

* `docs/docs/concepts/plugins.md` : une section « C ABI and SDK » (règles, versions, `abi` dans le manifeste, différences avec le chemin C++).
* `docs/docs/tutorials/write-a-game-with-the-sdk.md` : « Write a game with the SDK » à partir du gabarit.
* `docs/docs/api_reference.md` : la référence des tables, générée depuis les commentaires des en-têtes C (Doxygen sur `sdk/include` seulement).

## 8. Décisions à prendre avant de commencer

| Question | Recommandation |
|---|---|
| Garder le chemin C++ (tier 0, `Engine` lié) ? | **Oui.** Le jeu et les modules du moteur en ont besoin (accès complet, pas de couche) ; le chemin C est pour les tiers. Les deux partagent le manager et les registrars |
| Faire passer les modules internes par l'ABI C ? | Non en v1. Un seul vrai test : le module Voxel (R8) |
| Composants d'acteur ou composants ECS en v1 ? | Les deux exposés par le **même** descripteur : enregistrer un type C crée le type réflexion (acteur, sauvegarde, inspecteur) et un `ComponentId` ECS par nom. À valider sur un prototype en A2 avant de figer `shard_components.h` |
| Les noms de types C sont-ils préfixés par l'id du plugin ? | Oui, imposé (`voxel.Chunk`) : c'est déjà la règle des cvars et commandes |
| JSON dans l'ABI (`defaultJson`, `shard.state`) ? | Accepté comme **texte** : le format est stable, le parseur reste dans l'hôte |
| Un `ShardHostApi` par plugin ou un seul ? | Un seul, statique ; le plugin se désigne par `ShardPluginCtx`. Plus simple à geler, et prêt pour R6 (imports WASM) |
| Garder `PluginDesc` tel quel ? | Oui, pour le chemin `cpp` ; ne plus y ajouter de champ, tout ajout va dans l'ABI C |

## 9. Étapes

Durées pour une personne, environ 4 semaines au total, en accord avec R2. Chaque étape finit sur quelque chose qui se teste seul.

### A0 · Gel des règles (2 jours)

* `shard_abi.h` et `shard_host.h` écrits et relus ; cette note mise à jour avec ce qui a changé.
* Test `tests/plugins/abi_layout_tests.cpp` : `static_assert` sur `sizeof`/`offsetof` de chaque struct publique, et un fichier `.c` compilé en C11 qui inclut tous les en-têtes (`-Wall -Wextra -pedantic -Werror`).
* **Sortie** : les en-têtes de base compilent en C et en C++, leurs dispositions sont figées par un test.

### A1 · Chargement et base (3 à 4 jours)

* Champ `abi` dans `PluginManifest` + `ParseManifest` + tests de `manifest_tests`.
* Branche `c1` de `LoadLibrary`, `CAbiPlugin`, `query_interface`, `shard.plugin`, journal, mémoire.
* Fixture `tests/plugin_fixtures/c_plugin/` **écrite en C** : elle journalise à chaque étape du cycle de vie et alloue par l'hôte.
* **Sortie** : `dynamic_plugin_tests` charge, désactive, réactive et décharge un plugin C ; refus d'un `abiMinor` trop grand et d'un `structSize` trop petit.

### A2 · Composants, ECS et systèmes (1 semaine)

* `shard.components`, `shard.ecs`, `shard.systems`, adaptateurs, handles.
* Prototype de la décision « composant d'acteur + ECS par nom » (§8) ; si elle ne tient pas, on ne garde que les composants d'acteur en v1 et on le note.
* **Sortie** : la fixture C déclare un composant, un système le modifie, le monde est sauvé puis rechargé avec les valeurs ; `--check-plugins` liste le composant et le système.

### A3 · Événements, hooks, cvars, commandes, services, extensions, état (4 à 5 jours)

* Canal nommé dans `EventDispatcher` (avec tests propres, il sert aussi au C++).
* Structs C des hooks ; items C de `tool.validator`, `asset.type`, `asset.importer`.
* `shard.state` et rechargement à chaud.
* **Sortie** : deux fixtures C, l'une fournit un service, l'autre l'utilise ; désactiver la première vide la référence de la seconde ; un validateur C tourne dans `ShardAssetValidator` (étendre `Tools.AssetValidator*`) ; un rechargement garde l'état.

### A4 · SDK C++, CMake et gabarit (4 jours)

* `sdk/include/shard/cpp/`, `ShardSDKConfig.cmake`, `shard_sdk_add_plugin`, règle d'installation.
* Réécrire une fixture avec le SDK C++ (le plugin `Spin` du §7.2).
* **Sortie** : `cmake --install` produit un SDK ; le gabarit configure et compile contre ce SDK installé.

### A5 · Compatibilité et preuve hors dépôt (3 à 4 jours)

* **Hors dépôt** : un job CI configure `sdk/templates/plugin` dans un dossier temporaire avec seulement le SDK installé (aucun chemin vers `src/`), compile, et `ShardHeadless --check-plugins` le charge.
* **Autre compilateur** : sous Linux, plugin compilé avec Clang chargé par un moteur GCC ; sous Windows, plugin MinGW chargé par un moteur MSVC (dépend de R0 : la CI Windows doit exister). C'est le test qui prouve que l'ABI est réellement en C.
* **1.0 → 1.1** : copie figée des en-têtes 1.0 dans `tests/plugin_fixtures/abi_1_0/` ; une fixture compilée contre elle doit charger dans le moteur courant. On ajoute une fonction à une table (passage à 1.1) dans la même étape pour vérifier que la fixture 1.0 charge toujours et qu'une fixture 1.1 est refusée par un hôte 1.0 simulé.
* Fuzz léger du chargement : `ShardPluginInfo` avec `structSize`, pointeurs ou versions incohérents.
* Doc et tutoriel (§7.3).
* **Sortie** : les trois tests de compatibilité passent en CI ; le tutoriel est suivi pas à pas sur une machine propre.

## 10. Critère de réception

Le module **Voxel** (R8, phase 9 du tableau d'architecture) s'écrit contre le SDK seul : « builds with zero engine-internal includes ». S'il lui manque quelque chose, ça va dans une 1.1 de l'ABI, pas dans un include interne. Tant que Voxel n'existe pas, la fixture `Spin` et le gabarit tiennent lieu de preuve.

## 11. Risques

| Risque | Parade |
|---|---|
| Geler trop tôt une API qui bouge encore (ECS, réflexion) | La v1 n'expose que ce qui a des tests depuis plusieurs mois ; le reste passe par une interface `shard.experimental.*` sans garantie de compatibilité, refusée en tier 1 signé |
| Coût par appel (indirection, handles, copies de hooks) dans les systèmes | Itération par blocs d'archétype (un appel par bloc, pas par entité) ; mesurer sur la fixture `Spin` avec 100 000 entités contre le même système en C++ |
| Une struct modifiée par erreur sans changer de version | `abi_layout_tests` + revue des en-têtes `sdk/` obligatoire (CODEOWNERS ou règle dans `contributing.md`) |
| Deux implémentations qui divergent (C et C++) | L'adaptateur appelle les registrars ; aucune règle n'est réécrite côté C |
| Plantage dans un rappel C sans pile exploitable | Inchangé par rapport à aujourd'hui (R7 : attribution par adresse) |
| Le canal d'événements nommés double le travail de l'`EventDispatcher` | Il remplace à terme l'indexation par `typeid` aussi pour le C++, ce qui règle la question MSVC |

## 12. Ce que ça débloque ensuite

* **R6 (sandbox WASM)** : la table d'imports d'un module WASM = les interfaces de §5.2, avec les mêmes handles et `ShardStr` ; seule la mémoire change (pointeurs dans la mémoire linéaire de l'instance).
* **Liaisons d'autres langages** (Rust, Zig, C#) générées à partir des en-têtes C.
* **Fin de `HostLinks`** pour les tiers ; avec la suppression de `GetEngine()` (phase 2 du tableau), il pourra disparaître tout à fait.

## 13. Avancement

### Fait : A0 et A1 (2026-10-21)

* `sdk/include/shard/shard_abi.h` (types, versions, codes de résultat), `shard_host.h` (`ShardHostApi`, `ShardPluginInfo`, `ShardPluginCallbacks`, point d'entrée), `shard_plugin.h` (interface `shard.plugin` 1.0). Les en-têtes compilent en C11 pedantic (la fixture est en C avec `-Wall -Wextra -pedantic -Werror`) et en C++.
* `tests/plugins/abi_layout_tests.cpp` fige `sizeof`/`offsetof` de chaque struct ; à mettre à jour avec toute révision de l'ABI.
* Manifeste : champ `abi` (`"cpp"` par défaut, `"c1"`), `entry` par défaut `shard_plugin_entry` pour `c1`. `shard_add_plugin(... ABI c1)` écrit le champ et ne lie pas `Engine` (seulement `sdk/include`).
* `src/engine/plugins/abi/c_plugin.{hpp,cpp}` : `GetCHostApi()` (log, alloc/free comptés par `PluginMemory`, `query_interface`), `ValidateCPluginInfo` (version, taille, callbacks), `CAbiPlugin : IModule`. La branche `c1` de `PluginManager::LoadLibrary` n'applique pas l'empreinte de build. `ShardPluginCtx` = id du propriétaire, résolu dans une table à chaque appel : un handle resté après le déchargement ne trouve rien.
* Fixture `tests/plugin_fixtures/c_plugin/c_plugin.c` et `tests/plugins/c_plugin_tests.cpp` : cycle de vie complet, désactivation/réactivation, mémoire de l'hôte, refus d'un `abiMinor` trop grand, d'une autre majeure, d'un `structSize` trop petit, de callbacks absents, d'une entrée qui refuse.

### Fait en plus (2026-10-21) : cvars et commandes

* `shard_cvars.h` (`shard.cvars` 1.0) : `create_bool/int/float/string` (bornes, drapeau `SHARD_CVAR_READONLY`), `get_*`, `set_*`. Les handles `ShardCVar` sont `génération << 32 | slot` : détruits avec le contexte (`on_disable` / `on_unload`), un handle resté renvoie `SHARD_E_STALE_HANDLE`. `set_*` passe par `ForceSet` (le plugin est le propriétaire : il contourne `ReadOnly` et les couches de configuration, les bornes s'appliquent).
* `shard_commands.h` (`shard.commands` 1.0) : `add(name, help, run, user)`, les mots arrivent en `ShardStr[]`, la sortie est un `ShardStr` copié au retour, un code non nul = échec. Doublon dans le même plugin : `SHARD_E_ALREADY_EXISTS` ; un nom pris par le moteur ou un autre plugin est **remplacé** (comportement existant de `CVarRegistry::RegisterCommand`, non changé ici) : le préfixe reste la règle.
* Tests : `CPluginTest.*` (15 de base + 3 cvars + 1 commande), disposition épinglée pour `ShardCVarsApi` et `ShardCommandsApi`.

### Fait : A2 à A5 (2026-10-21)

**A2, composants, ECS, systèmes.** `shard_components.h`, `shard_ecs.h`, `shard_systems.h` ; côté hôte `src/engine/plugins/abi/c_components.cpp` et `c_systems.cpp`.

* La décision du §8 (« composant d'acteur + ECS par nom ») tient. `register_type` crée un `ComponentId` ECS nommé `c:<id>.<nom>` (le même id d'une activation à l'autre, si la taille n'a pas changé) et un composant d'acteur `CAbiComponent` dont la réflexion est construite à partir des `ShardFieldDesc`. Le composant d'acteur pousse ses valeurs sur l'entité de l'acteur (attachement, chargement, édition, activation) et les relit depuis l'entité avant de sauver ou de copier : ce qu'un système a écrit est ce qui est sauvé. Un composant désactivé quitte l'entité. Il a fallu un point d'accroche `Component::OnAttached()` (appelé par `Actor::AttachComponent` et `Actor::Clone`).
* `Registry::EachChunkRaw` / `CountRaw` : l'itération par blocs à partir d'identifiants, pour le C.
* Un `ShardEcs` désigne l'exécution d'un système sur un fil (`thread_local`) : gardé après, ou utilisé ailleurs, il renvoie `SHARD_E_STALE_HANDLE`. Un système n'atteint que les types qu'il a déclarés (sauf exclusif). Une fonction `main` appelée depuis un système renvoie `SHARD_E_WRONG_THREAD` (toujours, pas seulement en debug).
* Le compteur d'erreurs : un `run` qui renvoie une erreur passe par l'enveloppe du `SystemRegistrar`, comme une exception C++ (dixième erreur : le plugin échoue).
* Sortie vérifiée : `CAbiTest.*` (`tests/plugins/c_abi_interfaces_tests.cpp`) ; le composant est sauvé avec ce qu'un système a écrit et rechargé sur une autre entité ; `plugin.info <id>` liste `component` et `system`.

**A3, le reste des interfaces.** `shard_events.h`, `shard_hooks.h`, `shard_services.h`, `shard_extensions.h`, `shard_content.h`, `shard_state.h` ; `c_events.cpp`, `c_services.cpp`, `c_extensions.cpp`, `c_content.cpp`.

* Canal nommé dans `EventDispatcher` (`subscribeNamed`, `emitNamed`, `NamedEvent`) et dans `EventRegistrar` (`SubscribeNamed`, `EmitNamed`, `Unsubscribe`) : le C++ s'en sert aussi. Un événement émis depuis un système est mis en file et livré en fin d'image (`PluginManager::EndFrame` → `FlushCEvents`), les files prises dans l'ordre des noms de systèmes.
* Deux fixtures C : `c_plugin` fournit le service `cplugin.math`, `c_consumer` l'utilise ; désactiver la première vide la référence de la seconde (`SHARD_E_NOT_FOUND`, pas un pointeur pendant). Un validateur C tourne dans `RunToolValidators` ; importeur et type d'asset sont enveloppés dans les items C++ existants ; un rechargement à chaud garde l'état (`shard.state`).
* `on_frame_begin/end` et `on_tool_task` : `CAbiPlugin` possède des objets `EnginePlugin` / `ToolPlugin` que le manager prend comme `enginePlugin` / `toolPlugin` (`GetCPluginFacets`), ce qui règle le double héritage noté plus bas.

**A4, SDK C++, CMake, gabarit.** `sdk/include/shard/cpp/` (header-only, C++17 : `plugin.hpp`, `component.hpp`, `system.hpp`, `cvars.hpp`, `events.hpp`, `services.hpp`, `content.hpp`, `string.hpp`, `detail.hpp`), `sdk/cmake/` (`ShardSDKConfig.cmake`, `ShardSDKConfigVersion.cmake`, `ShardSDKPlugin.cmake` avec `shard_sdk_add_plugin`), `sdk/templates/plugin/`, `sdk/VERSION`. `cmake --install build --component sdk --prefix <dir>` pose le SDK dans `<dir>/sdk` ; la cible `ShardSDKArchive` en fait `shard-sdk-<version>.zip`. Le `shard_add_plugin` du dépôt utilise désormais l'écriture de manifeste du SDK. La fixture `sdk_spin` est le plugin du §7.2 (`SdkSpinTest.*`).

**A5, compatibilité.**

* Hors dépôt : le test `Sdk.OutOfTree` installe le SDK seul, copie le gabarit dans un dossier à part, le configure et le compile contre le SDK installé, puis `ShardHeadless --check-plugins --plugin` le charge et compare 1 et N fils.
* Autre compilateur : vérifié sur cette machine, plugin MSVC 19.51 (lié à MSVCP140) et plugin Clang chargés par le moteur MinGW GCC. `-DSHARD_SDK_TEST_VCVARS=<vcvars64.bat>` ajoute le test `Sdk.OutOfTreeMsvc`, `-DSHARD_SDK_TEST_CXX=clang++` fait compiler `Sdk.OutOfTree` par Clang. La CI le fait (Clang sous Linux, MSVC sous Windows, `.github/workflows/build.yml`) et passe (2026-10-23).
* 1.0 → 1.1 : copie figée des en-têtes 1.0 dans `tests/plugin_fixtures/abi_1_0/include`, fixture compilée contre elle ; puis passage à 1.1 (`shard.plugin` 1.1 : `host`, `has_permission`). La fixture 1.0 charge et tourne dans le moteur 1.1, une fixture 1.1 est refusée par un hôte 1.0 simulé (`ValidateCPluginInfo(info, 0)`) : `AbiCompatTest.*`.
* Fuzz léger : 20 000 `ShardPluginInfo` incohérents, et chaque fonction de chaque table appelée avec des handles qui ne désignent rien et des pointeurs nuls : pas de plantage, toujours une erreur.
* Doc : `docs/docs/concepts/plugins.md` (« The C ABI and the SDK »), `docs/docs/tutorials/write-a-game-with-the-sdk.md` (« Write a game with the SDK »), `docs/docs/api_reference.md` (générée, voir les écarts), `docs/docs/contributing.md` (la règle de revue des en-têtes, cf. §11), `sdk/README.md`.

### Fait : le reste du plan, ABI 1.2 (2026-10-21)

Ce que la note prévoyait et que la première passe avait laissé :

* **Le gros composant (§5.3)** : `SHARD_FIELD_BLOB` et `blob_create` / `blob_data` / `blob_resize` / `blob_free` (`shard.components` 1.1). Un blob est un bloc d'octets que l'hôte garde hors du composant, compté dans le budget mémoire du plugin et rendu avec lui ; la limite de 1 Ko du bloc ECS reste, ce qui ne tient pas dedans va dans un blob. Le bloc reste copié par `memcpy` (deux blocs peuvent partager un blob) ; le composant d'acteur, lui, possède les blobs de ses champs blob : il les sauve en base64 avec le monde, une copie de l'acteur (mode jeu) en reçoit des copies, le retirer les libère. `Shard::Blob` dans le SDK C++.
* **`shard.experimental.*` (§11)** : `shard_experimental.h`. Majeure 0, donnée pour sa mineure exacte seulement, pas épinglée par les tests de disposition, pas comptée dans `SHARD_ABI_MINOR`, refusée (`SHARD_E_UNSUPPORTED`) à un plugin natif portant une signature valide d'un éditeur de confiance (`MayUseExperimental`). Première interface : `shard.experimental.world_ecs` 0.1, le registre du moteur depuis le fil principal hors système (`open` / `close`, changements structurels appliqués à `close`, `Shard::experimental::World` en C++) : le manque « ECS hors système » relevé plus haut, essayé avant d'être figé.
* **Attribution des plantages (§6)** : chaque appel de l'hôte vers un plugin C passe par un `ActiveScope` (`c_internal.hpp`) : commandes, événements, hooks, validateurs, importeurs, chargement et libération d'assets, en plus du cycle de vie et des systèmes. `PluginManager::ActivePlugin()` ; test `CAbiTest.EveryCallbackIntoThePluginNamesItForACrashReport`.
* **Événements décrits (§5.5)** : `describe` (`shard.events` 1.1), `EventRegistrar::DescribeNamed` côté C++, `EventDispatcher::describeNamed` / `namedLayout`, `Events::FormatNamedEvent`. La console s'en sert : `event.list`, `event.watch <nom>`, `event.unwatch <nom>`. Les replays n'enregistrent pas d'événements nommés aujourd'hui : rien à y afficher.
* **Archive SDK en CI (§7.1)** : `ShardSDKArchive` dans `build.yml`, publiée par OS (`shard-sdk-Windows`, `shard-sdk-Linux`).
* **Cible `package` (§7.1)** : `ShardPkg` lié en statique (MinGW) et installé dans `sdk/bin` ; le gabarit demande `PACKAGE` ; `Sdk.OutOfTree` construit l'archive avec le `ShardPkg` du SDK installé, et vérifie que la compilation n'a vu aucun chemin du dépôt (`compile_commands.json` et fichiers de réponse).
* **Le tutoriel suivi pas à pas** : déroulé à la main depuis `shard-sdk-1.2.0.zip` dans un dossier vierge, l'id changé. Il a trouvé la bibliothèque d'import (`.dll.a`) embarquée dans le `.shardplugin` (corrigé : elle reste dans le dossier de build) et deux imprécisions du texte (corrigées). Ce n'est pas une machine propre : le moteur et MinGW étaient déjà là.
* Trouvé en route : `ShardHostApi::engineVersion` n'était jamais rempli (il l'est, depuis `config.h`), et `PluginContext::Engine()` restait nul pour tout plugin chargé avant le moteur, donc pour tous (`BindEngine` reçoit maintenant le moteur) : `get_asset` n'aurait jamais marché en vrai.

### Fait : ABI 1.3 (2026-10-21)

Deux manques empêchaient d'écrire du gameplay : déplacer un acteur, et avoir dans un composant autre chose que des nombres.

Déplacer un acteur : `shard.ecs` 1.1, `set_local_transform(ecs, entity, pose)`. `shard.LocalTransform` est un miroir que le `Transform` de l'acteur écrase, donc un système ne peut pas écrire dans son bloc. Il enregistre une pose, qui est appliquée à la fin de la phase, sur le fil principal, après les changements structurels (`Scheduler::SetPhaseEndHook`, installé par le manager, qui appelle `FlushCPoses`). L'application passe par `Transform::SetPosition`, `SetRotation` et `SetScale`, donc le miroir, les enfants, la physique et le rendu suivent, et seul ce qui a changé est appliqué. Les poses sont rangées par nom de système et appliquées dans cet ordre : le dernier nom gagne, quel que soit l'ordre de fin des systèmes. Le système doit déclarer `shard.LocalTransform` dans ses `writes` (sinon `SHARD_E_INVALID_ARG`). Une entité sans acteur répond `SHARD_E_NOT_FOUND`. Il n'y a pas de latence d'une image : la phase suivante voit déjà la pose.

Texte et assets : `shard.components` 1.2.
* `SHARD_FIELD_STRING` est un handle de blob dont les octets sont du texte UTF-8. Le composant d'acteur garde en plus un `std::string` par champ (8 au plus, `SHARD_COMPONENT_MAX_STRINGS`) que la réflexion lit : l'inspecteur le montre comme une zone de texte et le monde le sauve comme une chaîne. `Push` écrit le texte dans le blob avant de copier le bloc sur l'entité, `Pull` relit le blob après ce que les systèmes ont écrit. La propriété est celle des blobs (copie à `Clone`, libération à `Destroy`). Un champ `STRING` ne peut pas être `PRIVATE` : un blob convient dans ce cas. `string_set` et `string_get` évitent de manipuler le blob à la main.
* `SHARD_FIELD_ASSET` est un `int32_t` mappé sur `AssetID` / `TypeID::Asset`. L'inspecteur montre un sélecteur d'asset, le monde sauve le nom dans le projet, et le champ compte dans les dépendances d'assets.
* Ni l'un ni l'autre ne peut figurer dans un événement décrit.

Le SDK C++ gagne `Shard::Text`, `Shard::AssetId`, `Shard::Pose`, `Components::SetText` et `GetText`, `SystemCtx::SetText`, `GetText` et `SetLocalTransform`. Dans un moteur plus ancien que la 1.3 ils lèvent `Shard::Error` (`SHARD_E_UNSUPPORTED`).

Tests : `CAbiTest.ATextAndAnAssetAreSavedShownAndOwnedByTheActorComponent`, `CAbiTest.ASystemMovesTheActorBehindAnEntityAtTheEndOfThePhase` (fixture C, type `Label`), `SdkSpinTest.ATextAnAssetAndAMovingSystemWrittenWithTheSdk` (fixture C++), un test du hook de fin de phase dans `EcsScheduler`, les dispositions de `ShardEcsApi` (96 octets) et `ShardComponentsApi` (104), et le fuzz des handles.

Pas fait en 1.3 : `SHARD_FIELD_ENTITY` (une référence à un autre acteur demande une résolution au chargement du monde, un remappage quand l'acteur est copié en mode jeu et un sélecteur d'acteur dans l'inspecteur), `TAG`, la promotion de `world_ecs`, la création et la destruction d'acteurs, l'input, la physique et l'audio.

### Fait : ABI 1.4, un jeu en plusieurs fichiers (2026-10-21)

Le SDK 1.3 était une API d'enregistrement : `OnLoad` pose des lambdas et tout tient dans `plugin.cpp`. Le moteur a pourtant déjà le modèle qu'il faut : le composant `Script` (`OnCreate`, `OnPlay`, `OnTick`, `OnFixedTick`, `OnLateTick`, `OnStop`, `OnDestroyed`), des champs décrits par réflexion et une classe par fichier (`src/apps/game`). Le SDK reprend ce modèle.

`shard.scripts` 1.0 (`shard_scripts.h`, `c_scripts.cpp`) :
* Un type de script est un composant d'acteur qui dérive du `Script` du moteur (`CScriptComponent<N>`, avec N = 256 octets, 1 Ko, 4 Ko ou 16 Ko : la plus petite taille qui contient l'objet, le stockage étant au même endroit dans le composant pour toutes). Le monde le tick comme les siens, le mode jeu l'appelle, et `Clone` construit un objet neuf puis recopie les champs décrits (`CopyFields` copie les octets, et les blobs et textes pour de bon).
* L'objet est construit et détruit sur place (`construct(user, storage)`) et n'est jamais déplacé, donc il peut contenir des `std::vector`.
* Les champs utilisent `ShardFieldDesc`, texte, asset et blob compris. Le code qui les valide est partagé avec les composants (`BuildFieldSet`, `c_fields.hpp`, dont dérivent `CType` et `CScriptType`).
* Un rappel qui renvoie une erreur ou lève passe par `reportError`, le dixième fait échouer le plugin, et l'appel est entouré d'un `ActiveScope` pour attribuer les plantages. Quand le contexte du plugin se libère, `ReleaseScripts` marque ses types comme morts : plus aucun rappel, même pour les objets qu'un monde tient encore.

`shard.actors` 1.0 (`c_actors.cpp`) :
* `ShardActor` est une génération et une case dans une table de `weak_ptr<Actor>`. Les ids d'objets du moteur sont réutilisés, donc un id ne peut pas servir de handle.
* Pose locale et monde, hiérarchie, nom, activation, tags, destruction.
* `create`, `spawn` (JSON d'un acteur de fichier monde, via `InstantiateActor`), `find_by_name` et `find_by_tag` travaillent dans le monde d'un acteur « ancre ». Il n'y a pas de handle de monde parce qu'un monde peut être déchargé, alors qu'un acteur invalide le dit.
* Les composants du moteur s'atteignent par leur nom de sérialisation, avec leurs champs en JSON. `set_component` fusionne avec ce que le composant écrit déjà, donc les autres champs gardent leur valeur.
* Ce qui n'est pas un champ (forces, raycasts) n'y est pas : ce sera `shard.physics`.

Fils : la mention « thread : script » (`OnScriptThread`) vaut dans un rappel de script (`ScriptScope`) ou sur le fil principal hors système. Les systèmes `Scripts.*` du moteur sont exclusifs mais pas `mainThread`, donc ils peuvent tourner sur un fil de travail : c'est pourquoi la portée est un `thread_local` et pas une comparaison avec le fil principal. Un système C n'y a pas accès (`SHARD_E_WRONG_THREAD`).

`shard.input` 1.0 (`c_input.cpp`) : le gestionnaire d'entrée du moteur, celui que les replays enregistrent, avec des numéros de touches propres à l'ABI (ceux de GLFW) et une table vers `Input::Key`. Il lit `Core::gSharedEnginePtr` plutôt que `GetEngine()`, qui arrête le processus quand il n'y a pas de moteur.

Correction dans le moteur : `World::Tick` et les boucles voisines parcouraient `scripts` directement, alors que `RemoveComponent` y efface. Un script qui détruisait son propre acteur provoquait un comportement indéfini. Les boucles prennent maintenant un instantané des scripts et sautent ceux retirés entre-temps (`ForEachScript`).

SDK C++ :
* `shard/game.hpp` inclut le reste : `cpp/script.hpp` (`Shard::Script`, `SHARD_REGISTER_SCRIPT` et `_EX`), `cpp/actor.hpp` (`Actor`, `Scene`), `cpp/input.hpp`, `cpp/math.hpp` (`Vec2`, `Vec3`, `Quat`, `Pose`, `LookRotation`) et `cpp/registry.hpp`.
* Chaque `.cpp` ajoute sa fonction d'enregistrement à une liste à l'initialisation statique (`SHARD_REGISTER_SCRIPT`, `SHARD_DEFINE_COMPONENT`, `SHARD_ON_LOAD(Nom, ctx)`). Le SDK la déroule au début d'`OnLoad`, triée par clé (`0component:`, `1script:`, `3load:`) et non dans l'ordre d'édition de liens, parce que l'ordre des ids ECS et des systèmes en dépend, donc le déterminisme.
* `GetScript<T>()` retrouve le nom sous lequel T a été enregistré par `typeid`. L'en-tête de T n'a donc pas besoin de déclarer un trait.
* `Shard::Text` a `Get()` et `Set()` : le SDK connaît le contexte du plugin (`detail::PluginHandle()`).
* `SHARD_GAME_MODULE()` fait un plugin qui n'est que ces enregistrements.

CMake : `shard_sdk_add_plugin(... SOURCE_DIRS src)` prend tous les fichiers sous `src/` (avec `CONFIGURE_DEPENDS`). Le gabarit est maintenant un projet de plusieurs fichiers (`module.cpp`, `gameplay/spinner.*`, `ecs/drift.cpp`).

Tests : `GameScriptsTest.*` (fixture `tests/plugin_fixtures/game_scripts`, dix fichiers dont ceux du tutoriel, compilés avec `-Werror`), le fuzz des handles, les dispositions des nouvelles tables, et `Sdk.OutOfTree` avec le nouveau gabarit.

Pas fait en 1.4 (fait en 1.5, voir plus bas) : les contacts physiques dans les scripts, `shard.physics`, l'audio, le chargement d'un monde, une référence à un acteur dans un champ, un `std::string` en membre listé d'un script, les prefabs par nom et une API de widgets pour l'éditeur. Toujours pas fait : `TAG` en champ.

### Fait : ABI 1.5, physique, audio, mondes, éditeur (2026-10-21)

Tout tient dans une seule révision : `shard.physics`, `shard.audio`, `shard.worlds` et `shard.editor` en 1.0, `shard.scripts` 1.1, `shard.components` 1.3.

* **Contacts** : `ShardScriptCallbacks` gagne `on_contact_added/persisted/ended` (la struct passe de 120 à 160 octets, les champs sont ajoutés à la fin ; un plugin plus ancien a un `structSize` plus petit et ces champs sont lus comme nuls). `CScriptBase` surcharge les `OnContact*` de `Script` et fabrique un `ShardContact`. Jolt donne la normale du corps 1 vers le corps 2 : elle est retournée quand le script est le second, pour qu'elle aille toujours de soi vers l'autre. La fin d'un contact n'a pas de manifold, donc ni point ni normale.
* **Physique** : les fonctions passent par les méthodes de `PhysicsBody`. `PhysicsManager::RayCast` rend maintenant le point et la normale (lus sous `BodyLockRead`). Le tir ignore un acteur (`IgnoreSingleBodyFilter`, `Jolt/Physics/Body/BodyFilter.h`) pour qu'un script ne se touche pas lui-même.
* **Audio** : `AudioManager::PlayOneShot` crée un son sans acteur et le retire dans `Tick()` quand `ma_sound_at_end` dit qu'il est fini. Un gestionnaire « silencieux » (pas de périphérique : serveur, tests) accepte tous les appels.
* **Mondes** : `load_world` passe par `LoadWorldAsync`, et le monde est échangé dans la mise à jour de monde de la phase PreSim, pas au milieu de l'image du script qui l'a demandé. Les prefabs se lisent dans les `ContentMounts` (`mod://`, chemin logique `prefabs/x.json`, ou nom nu) puis passent par `Worlds::InstantiateActor`.
* **Références d'acteur** (`SHARD_FIELD_ACTOR`, scripts seulement) : le champ est un `ShardActor` dans l'objet et un chemin `Racine/Enfant` dans la sérialisation. Le chemin est gardé dans l'un des 8 emplacements de texte (partagés avec les chaînes) et rendu comme une chaîne éditable dans l'inspecteur. Si l'acteur n'existe pas encore (chargement, copie en mode jeu), le champ reste « en attente » et est résolu avant `OnCreate`, `OnWorldLoaded`, `OnPlay`, `OnTick` et `OnFixedTick`. Un composant simple ne peut pas en avoir : son bloc est copié par `memcpy` et ne peut pas porter de chemin.
* **`std::string` listé** (`SHARD_FIELD_ACCESSOR`) : le champ n'est pas un handle ; l'hôte lit et écrit le texte par `text_get` / `text_set` du script, donc la `std::string` ne traverse jamais l'ABI, seuls les octets. Un type qui enregistre un champ de ce genre doit fournir les deux callbacks.
* **Panneaux** : ImGui reste dans l'exécutable de l'éditeur. Le moteur expose `EditorUiBackend` (`ShardUiApi` + `begin` / `end`) que l'éditeur installe après `ImGui::CreateContext()` (`InstallPluginUi`). Sans backend (joueur, headless, outils), `shard.editor` répond `NOT_FOUND` et `Editor::AddPanel` renvoie faux : le plugin charge partout. Les panneaux sont des items d'extension `editor.panel` et apparaissent dans le menu Fenêtre.
* **Piège** : ces appels passent par `Core::gSharedEnginePtr`, pas `GetEngine()`, qui termine le processus (`exit(2)`) quand il n'y a pas de moteur (outil hors ligne, test).
* **SDK C++** : `Actor::AddForce`, `AddImpulse`, `Velocity`..., `Physics::Raycast`, `Audio::PlayOneShot`, `Worlds::Load`, `Scene::SpawnPrefab`, `Script::OnContact*`, `Shard::Actor` et `std::string` comme champs listés, `SHARD_REGISTER_SCRIPT_BARE`, `SHARD_EDITOR_PANEL`.

Tests : `AbiLayout` (les nouvelles tailles et offsets sont figés), `TheInterfacesOfEachRevisionAreOffered`, le fuzz étendu aux nouvelles tables, `GameScriptsTest.*` (références d'acteur et texte, audio / prefab / monde, panneaux avec un faux éditeur) et `GamePhysicsTest.AScriptPushesBodiesCastsRaysAndHearsContacts` avec un vrai `PhysicsManager`. Piège de test : une copie d'acteur n'enregistre ses scripts que dans un monde « chargé » (`World::SetLoaded(true)`).

### Écarts par rapport au plan

* `CAbiPlugin` dérive d'`IModule` ; les rappels d'`EnginePlugin` et de `ToolPlugin` passent par des objets à part (voir A3).
* Les callbacks renvoient un `ShardResult` : une valeur non nulle fait échouer le plugin comme une exception en C++ (sauf `on_tool_task`, dont l'erreur annule la tâche).
* `ShardPluginInfo::structSize` est écrit par le plugin (son `sizeof`), pas par l'hôte : c'est ce qui permet de refuser une struct trop petite.
* Le lien `ShardPluginCtx` → contexte ne s'arrête plus dans `on_disable` mais quand le contexte libère tout (`PluginContext::OnRelease`, premier enregistrement donc dernier libéré) : `on_unload` peut encore libérer sa mémoire, les ressources d'un type d'asset sont rendues au plugin et ses blobs libérés tant que le contexte existe.
* Composants : des champs de données simples (nombres, booléen, vecteurs, quaternion, couleur), des blobs, et depuis la 1.3 du texte et des assets. Pas de `ENTITY`, `TAG`, donc pas de `construct` / `destroy` / `copy` ; les valeurs par défaut sont un bloc d'octets, pas du JSON. Drapeaux : `RUNTIME` (affiché en lecture seule, non sauvé) et `PRIVATE` ; pas de `HIDDEN` (l'inspecteur ne sait pas cacher un champ). Les noms sont préfixés par l'hôte (`<id>.<nom>`), comme les cvars.
* `shard.ecs` ne sert que depuis un système ; le fil principal passe par `shard.experimental.world_ecs`, sans promesse.
* Hooks : une seule struct `ShardHookArgs` (`subject`, `detail`, `frame`) décrite par un tableau dans l'en-tête, au lieu d'une struct par hook.
* Extensions : `add_validator`, `add_asset_type`, `add_importer` typées plutôt qu'un `add(point, name, item, item_type)` générique ; plus `get_asset` pour relire une ressource.
* Services : un service C est un `ShardService` (table + `self` + version) dont l'hôte garde la copie ; `resolve` refuse (`SHARD_E_UNSUPPORTED`) un service fourni par un plugin C++.
* La description d'un événement réutilise les `ShardFieldDesc` des composants (sans blob : une charge est copiée).
* La référence de l'API (`docs/docs/api_reference.md`) est générée depuis les commentaires des en-têtes par `scripts/gen_api_reference.py`, pas par Doxygen : le site est en MkDocs (du Markdown, pas le HTML de Doxygen), le script n'ajoute aucune dépendance, et il vérifie au passage que la révision annoncée par chaque en-tête est celle que l'hôte offre. Le test `Docs.ApiReference` échoue si la page n'est plus ce que donnent les en-têtes ; le workflow du site la régénère avant de construire.

### Reste

* ~~CI~~ : fait. La CI construit et teste la 1.5 sous Windows (MinGW) et Linux (GCC), et tout passe (2026-10-23) : tests, replay de déterminisme, étapes « autre compilateur » (Clang sous Linux, MSVC sous Windows) et archive SDK. Le seul écart Linux/Windows rencontré en route était le hachage des scripts (`-0.0` contre `+0.0` dans un champ, réglé par `CanonicalizeFloats`, replay doré réenregistré).
* Mesurer le coût par appel sur `Spin` avec 100 000 entités contre le même système en C++ (risque du §11).
* Le critère de réception du §10 (le module Voxel contre le SDK seul) attend le module ; les blobs sont là pour lui.
* Le tutoriel sur une machine réellement vierge (sans MinGW ni moteur installés à côté).
* Essayer les panneaux dans l'éditeur lui-même (les tests passent par un faux backend ; le dessin ImGui réel n'a été que compilé).
* Faire passer `world_ecs` de l'expérimental au figé une fois éprouvé.
* Compiler les extraits de la section 8 du tutoriel (`Turret`, le panneau) : ils sont écrits à partir de l'API, pas copiés d'une fixture.
* Figer une copie des en-têtes 1.4 à côté de celle de la 1.0 (`tests/plugin_fixtures/abi_1_0`), pour que les tests de compatibilité vérifient aussi qu'un plugin compilé en 1.4 charge toujours.

### Le module de jeu de l'exemple écrit avec le SDK seul (2026-10-22)

`src/apps/game` (`Character`, `Gun`, `RandomKicker`) n'inclut plus rien du moteur : `ABI c1` dans `shard_add_plugin`, `#include <shard/game.hpp>`, une ligne `SHARD_GAME_MODULE()` (`module.cpp`). Les `.reflection.hpp` générés et `game_module.cpp` (`LinkJolt`, `AdoptStaticRegistrars`) ont disparu.

* **Noms** : un script du SDK s'appelle `<id du plugin>.<nom>`, donc les mondes disent `game.Character`, `game.Gun` et `game.RandomKicker` (`physics.world`, `sponza.world`, `determinism.world` ont été réécrits). Un monde ancien qui dit `Character` garde le composant tel quel, comme n'importe quel type inconnu, et ne marche plus.
* **Ajouts au SDK qu'il a fallu** : `shard.plugin` 1.2 `random_stream` (le `RandomKicker` tirait son générateur de `Core::GetEngine().MakeRandomStream`) et `Shard::Random` (`cpp/random.hpp`), le PCG32 du moteur dans le plugin : mêmes bits, vérifié par `GameScriptsTest` contre `Core::Random`. Le nom du flux porte maintenant l'id du plugin (`game/RandomKicker/<acteur>`) et plus l'id local du composant, que le SDK ne donne pas ; `golden/determinism.replay` a donc été enregistré de nouveau (`--replay` et `--write-replay` sur le même fichier : les entrées WASD sont gardées, seuls les contrôles d'état changent, et le fichier note maintenant la ligne `plugin game`). Les deux scripts comptent dans le hachage (le `Character` marche avec ces touches) : sans le module de jeu, la rediffusion diverge. Rien ne dit que le `Character` du SDK donne les mêmes bits que l'ancien, il donne les mêmes à chaque exécution.
* **Petits écarts** : `Radians` / `Degrees` du SDK multiplient par le facteur écrit en double (`kPi / 180.f` donnait un ulp d'écart avec glm), `FieldOptions` a un constructeur (`{ "frames", SHARD_FIELD_RUNTIME }` ne déclenche plus `-Wmissing-field-initializers`), `lockedMouseX`, `pitch`, `frames`... sont `RUNTIME` (affichés, plus sauvés dans le monde).
* **`Gun`** : la balle est un acteur créé par `Scene::Create`, puis (à l'origine) `AddComponent("model", {"mesh": <id>, "materials": [<id>]})` et `AddComponent("physics_body", ...)` en JSON (le moteur lit un entier comme un nom d'asset), puis `SetVelocity`. Le corps existait dès que le composant existait. Remplacé ensuite par les composants typés, voir plus bas. Si un composant échoue, la balle est détruite avant de relancer l'exception. Le champ `sound` existait déjà sans servir : il reste sans servir.
* **`--game`** accepte une bibliothèque de l'ABI C sans manifeste : `PluginManager::LoadLibrary` regarde si elle exporte `shard_plugin_entry` et pas `shard_plugin_describe`.
* **Tests** : `ExampleGameTest.*` charge `<build>/plugins/game` dans le contexte de test (avec `RegisterRenderingModule` et `RegisterPhysicsModule`) : `Character` avance de `speed * dt`, `Gun` tire une balle qui vole puis disparaît, `RandomKicker` pousse dans la direction du flux du moteur.
* **Pas fait** : le `Character` lit le curseur en `float` (`CursorPosition`) là où le moteur le lisait en `double` ; sans conséquence tant que le curseur est en pixels entiers.

### Champs déclarés dans le header, composants typés (2026-10-22)

Deux retours sur le `Gun` : la liste `SHARD_REGISTER_SCRIPT_EX(Gun, (a)(b), ({ "a", 0, 0.f, 1000.f }, ...))` du `.cpp` est loin du membre, et `AddComponent("physics_body", R"({"motionType":...})")` n'est pas une API.

* **`SHARD_FIELD(Type, nom, options...) = valeur;`** (`cpp/field.hpp`). Dans la classe, avant chaque champ : la macro déclare le membre (`Type nom`, la valeur par défaut va après, comme pour n'importe quel membre) et, devant lui, un membre d'un octet (`FieldMarker`) dont le constructeur annonce nom, type, décalage et options. Pas de générateur de code (le SDK ne peut pas en imposer un à un jeu hors du dépôt) : `Scripts::Register` construit un objet témoin avec un collecteur actif, les marqueurs s'y inscrivent, et ceux des objets que l'hôte construit ensuite ne font rien (pas de collecteur, ou adresse hors du témoin). L'ordre des champs est celui de la classe. Les options s'écrivent nues (`Range(0.f, 1000.f)`, `Tooltip("...")`, `Runtime()`, `Hidden()`) : un lambda dans l'initialiseur du marqueur fait `using namespace Shard::field`, et ne s'évalue que pendant l'enregistrement.
* **`SHARD_REGISTER_SCRIPT(Classe)`** suffit alors. Les anciennes formes (`(a)(b)`, `_EX`, `_BARE`) marchent toujours et peuvent être mélangées avec des `SHARD_FIELD` dans une même classe (un nom déclaré deux fois est refusé).
* **Piège** : la macro finit par `Type nom`, donc un champ sans `= valeur;` vaut zéro tant que la classe n'a pas de constructeur à elle (l'hôte la construit avec `T()`), et n'importe quoi sinon. Le compilateur du projet est en `-pedantic -Werror` : une macro variadique appelée sans argument variadique est une erreur en C++17, donc `SHARD_FIELD` ajoute elle-même une option vide (`End()`) à la fin. `Scripts::Register` donne au collecteur l'adresse du témoin sous forme d'entier (`uintptr_t`) : lui passer le pointeur vers du stockage pas encore construit déclenchait `-Werror=maybe-uninitialized` sur certains GCC (signalé sur le build de l'utilisateur, jamais reproduit avec le GCC 15.2 de MSYS2 à -O0/-O2/-O3/LTO).
* **`shard.actors` 1.1** (dans la 1.5, pas encore publiée) : `get_component_field`, `set_component_field`, `erase_component_field`. Le chemin est un nom de champ (`"intensity"`) ou un chemin dans sa valeur (`"materials/0"`, `"shapes/0/params/radius"`, `"shapes/-"` pour ajouter), la valeur du JSON. Un champ réfléchi passe par `Component::ApplyField`, donc par `OnFieldChanged` comme une édition de l'inspecteur : changer le frottement d'un corps ne le reconstruit pas et garde sa vitesse. Ce que la réflexion ne décrit pas (les formes du `PhysicsBody`) passe par `Deserialize` sur le JSON complet modifié, comme `set_component`. Un tableau grandit pour contenir l'indice (les trous valent 0). Un champ inconnu est `NOT_FOUND`, une valeur du mauvais type `INVALID_ARG`.
* **Composants typés** (`cpp/engine_components.hpp`) : `Model`, `PhysicsBody` (avec `Shape::Sphere/Box/Capsule/Cylinder`), `Light`, `Camera`, `AudioSource`, `AudioListener`, `Billboard`, par-dessus `ComponentRef` qui atteint n'importe quel champ de n'importe quel composant du moteur par son nom. `actor.Add<Shard::Model>()`, `Get<T>()` (le handle dit s'il y a un composant), `Has<T>()`, `Remove<T>()`. Les setters jettent `Shard::Error` en cas de refus (une faute de frappe est une erreur du script), les getters rendent zéro. Les assets se lisent par leur nom dans le projet (c'est ce que sérialise le moteur) et s'écrivent par `AssetId` ou par nom. Il n'y a pas encore de classe pour `ReverbZone`, `GravityField`, `ProbeVolume`, `ReflectionProbe` : `ComponentRef` les atteint.
* **Changement dans le moteur** : `PhysicsBody::OnFieldChanged("motionType")` reconstruit le corps tout de suite (au lieu de le marquer à refaire au prochain `Tick`), pour que `SetMotionType(Dynamic)` puis `SetVelocity` marchent dans la même fonction. Différence à vérifier à l'écran (pas fait) : l'ancien chemin du `Gun` passait par `Model::Deserialize`, qui finit par `DeserializeActive(data, false)` (composant inactif si le JSON ne dit pas `"active"`) ; le nouveau ne désérialise pas et laisse le composant actif.
* **`Gun`** réécrit : `bullet.Add<Shard::Model>()`, `SetMesh`, `SetMaterial(0, ...)`, `Add<Shard::PhysicsBody>()`, `SetShape(Shape::Capsule())`, `SetMotionType(Dynamic)`, `SetVelocity`. `Character` et `RandomKicker` passent aux `SHARD_FIELD`.
* **Tests** : `GameScriptsTest.FieldsAreDeclaredInTheClassWithTheirOwnOptions`, `TypedComponentsTest.*` (le script `Builder` du fixture, et les trois fonctions de l'ABI par chemin, y compris les refus et le handle périmé), `TheInterfacesOfEachRevisionAreOffered` et `abi_layout_tests` (`ShardActorsApi` passe de 216 à 240 octets).
* **Pas fait** : les composants plain data (`SHARD_COMPONENT`) gardent la liste `(a)(b)` : leur bloc est copié par `memcpy` et un marqueur d'un octet par champ le gonflerait. Pas de getter qui rende un `AssetId` (le moteur donne le nom). Le composant `camera` n'a pas pu être testé dans le contexte de test (il demande un `CameraManager`).

### Nettoyages du 2026-10-22

* **Namespace** : le SDK C++ est passé de `shard::` à `Shard::` (même casse que le moteur). Les en-têtes, le gabarit, les fixtures, le module de jeu, le tutoriel et la référence d'API ont suivi ; les noms C (`Shard*`, `shard_*`, `SHARD_*`) et les ids d'interfaces (`shard.physics`…) ne changent pas, donc l'ABI non plus.
* **Physique et audio sortis de `plugins/abi`** : `c_physics.cpp` et `c_audio.cpp` incluaient la physique et l'audio, qui sont au-dessus de la couche des plugins dans `scripts/check_deps.py`. Ils sont devenus `src/engine/c_abi_physics.cpp` et `src/engine/c_abi_audio.cpp` ; `c_internal.hpp` ne fait que déclarer `PhysicsApi()`, `AudioApi()` et `DecodeContact`. `c_scripts.cpp` n'inclut plus Jolt : un contact lui arrive en `ContactInfo` (autre acteur, normale, point, pénétration, présence d'un manifold), traduit à la racine du moteur ; l'orientation de la normale (de soi vers l'autre) reste faite par le script.
* **Handles d'acteur** (`c_actors.cpp`) : quand l'adresse d'un acteur détruit est reprise par un nouvel acteur avant le balayage, l'ancienne case est libérée tout de suite (`Release`), et l'index par adresse n'est retiré que s'il désigne encore la case libérée. Avant, la case morte restait occupée jusqu'au balayage et son entrée pouvait effacer celle du nouvel acteur.
* **`Actor::Destroy`** remet à nul le `parent` de ses composants : ce lien gardait l'acteur détruit en vie, et avec lui ce que le `weak_ptr` d'un `ShardActor` désigne. L'acteur et ses composants sont maintenant libérés (`ActorTest.ADestroyedActorAndItsComponentsAreFreedOnceTheWorldChanges`).

### Absent après la 1.5 (candidats pour une 1.6)

* **`TAG`** : pas de champ `TAG` dans les scripts ni dans les composants.
* **Physique** : pas de formes ni d'articulations. Le raycast ne rend que le premier impact, sans requête de recouvrement ni de balayage. La fin d'un contact n'a ni point ni normale (Jolt ne donne pas de manifold à ce moment).
* **Audio** : pas d'effets. Un son ponctuel ne rend pas de handle, donc on ne peut ni l'arrêter ni le régler après coup.
* **Références d'acteur** : réservées aux scripts (un composant est copié par `memcpy` et ne peut pas porter de chemin), et elles partagent avec les chaînes les 8 emplacements de texte.
* **Éditeur** : seulement des fenêtres avec des widgets simples. Pas d'extension de l'inspecteur, pas de gizmos, pas de menus personnalisés.
* **Hors périmètre de l'ABI C** : le rendu (features, pipelines) et le réseau. Un plugin qui en a besoin s'écrit en C++ contre le moteur.
