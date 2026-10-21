# ABI C stable et SDK public : plan

> Note interne, suite de `plugin-system.fr.md` (phase **R2**, phase 7 du plan d'origine). **État (2026-10-21) : A0 à A5 faits, puis le reste du plan, l'ABI est en 1.2** (voir §13 : ce qui a été fait, les écarts au plan et ce qui reste). Le reste du document est le plan tel qu'il a été écrit avant le code.
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

Un service C est une struct de pointeurs de fonctions avec `structSize` en tête, plus un `void* self`. Les deux plugins incluent le même en-tête de service (publié par le fournisseur, pas par le moteur). `ServiceRef` devient `ShardServiceRef` : un handle dont l'hôte remet le slot à `NULL` quand le fournisseur part, exactement comme `ServiceSlot` aujourd'hui. Le SDK C++ fournit `shard::Service<T>` par-dessus.

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
      plugin.hpp            classe shard::Plugin + macro SHARD_C_PLUGIN(Classe)
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

class SpinPlugin : public shard::Plugin {
public:
    void OnLoad(shard::Context& ctx) override
    {
        auto spin = ctx.Components().Register<Spin>("spin.Spin");
        ctx.Systems().Add("spin.Rotate", shard::Phase::Update)
            .Writes(spin)
            .Run([spin](shard::SystemCtx& s) {
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
* `docs/docs/tutorials.md` : « Write a plugin outside the engine repository » à partir du gabarit.
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
* Autre compilateur : vérifié sur cette machine, plugin MSVC 19.51 (lié à MSVCP140) et plugin Clang chargés par le moteur MinGW GCC. `-DSHARD_SDK_TEST_VCVARS=<vcvars64.bat>` ajoute le test `Sdk.OutOfTreeMsvc`, `-DSHARD_SDK_TEST_CXX=clang++` fait compiler `Sdk.OutOfTree` par Clang. La CI le fait (Clang sous Linux, MSVC sous Windows, `.github/workflows/build.yml`) : **pas encore vu tourner en CI**.
* 1.0 → 1.1 : copie figée des en-têtes 1.0 dans `tests/plugin_fixtures/abi_1_0/include`, fixture compilée contre elle ; puis passage à 1.1 (`shard.plugin` 1.1 : `host`, `has_permission`). La fixture 1.0 charge et tourne dans le moteur 1.1, une fixture 1.1 est refusée par un hôte 1.0 simulé (`ValidateCPluginInfo(info, 0)`) : `AbiCompatTest.*`.
* Fuzz léger : 20 000 `ShardPluginInfo` incohérents, et chaque fonction de chaque table appelée avec des handles qui ne désignent rien et des pointeurs nuls : pas de plantage, toujours une erreur.
* Doc : `docs/docs/concepts/plugins.md` (« The C ABI and the SDK »), `docs/docs/tutorials.md` (« Write a plugin outside the engine repository »), `docs/docs/api_reference.md` (générée, voir les écarts), `docs/docs/contributing.md` (la règle de revue des en-têtes, cf. §11), `sdk/README.md`.

### Fait : le reste du plan, ABI 1.2 (2026-10-21)

Ce que la note prévoyait et que la première passe avait laissé :

* **Le gros composant (§5.3)** : `SHARD_FIELD_BLOB` et `blob_create` / `blob_data` / `blob_resize` / `blob_free` (`shard.components` 1.1). Un blob est un bloc d'octets que l'hôte garde hors du composant, compté dans le budget mémoire du plugin et rendu avec lui ; la limite de 1 Ko du bloc ECS reste, ce qui ne tient pas dedans va dans un blob. Le bloc reste copié par `memcpy` (deux blocs peuvent partager un blob) ; le composant d'acteur, lui, possède les blobs de ses champs blob : il les sauve en base64 avec le monde, une copie de l'acteur (mode jeu) en reçoit des copies, le retirer les libère. `shard::Blob` dans le SDK C++.
* **`shard.experimental.*` (§11)** : `shard_experimental.h`. Majeure 0, donnée pour sa mineure exacte seulement, pas épinglée par les tests de disposition, pas comptée dans `SHARD_ABI_MINOR`, refusée (`SHARD_E_UNSUPPORTED`) à un plugin natif portant une signature valide d'un éditeur de confiance (`MayUseExperimental`). Première interface : `shard.experimental.world_ecs` 0.1, le registre du moteur depuis le fil principal hors système (`open` / `close`, changements structurels appliqués à `close`, `shard::experimental::World` en C++) : le manque « ECS hors système » relevé plus haut, essayé avant d'être figé.
* **Attribution des plantages (§6)** : chaque appel de l'hôte vers un plugin C passe par un `ActiveScope` (`c_internal.hpp`) : commandes, événements, hooks, validateurs, importeurs, chargement et libération d'assets, en plus du cycle de vie et des systèmes. `PluginManager::ActivePlugin()` ; test `CAbiTest.EveryCallbackIntoThePluginNamesItForACrashReport`.
* **Événements décrits (§5.5)** : `describe` (`shard.events` 1.1), `EventRegistrar::DescribeNamed` côté C++, `EventDispatcher::describeNamed` / `namedLayout`, `Events::FormatNamedEvent`. La console s'en sert : `event.list`, `event.watch <nom>`, `event.unwatch <nom>`. Les replays n'enregistrent pas d'événements nommés aujourd'hui : rien à y afficher.
* **Archive SDK en CI (§7.1)** : `ShardSDKArchive` dans `build.yml`, publiée par OS (`shard-sdk-Windows`, `shard-sdk-Linux`).
* **Cible `package` (§7.1)** : `ShardPkg` lié en statique (MinGW) et installé dans `sdk/bin` ; le gabarit demande `PACKAGE` ; `Sdk.OutOfTree` construit l'archive avec le `ShardPkg` du SDK installé, et vérifie que la compilation n'a vu aucun chemin du dépôt (`compile_commands.json` et fichiers de réponse).
* **Le tutoriel suivi pas à pas** : déroulé à la main depuis `shard-sdk-1.2.0.zip` dans un dossier vierge, l'id changé. Il a trouvé la bibliothèque d'import (`.dll.a`) embarquée dans le `.shardplugin` (corrigé : elle reste dans le dossier de build) et deux imprécisions du texte (corrigées). Ce n'est pas une machine propre : le moteur et MinGW étaient déjà là.
* Trouvé en route : `ShardHostApi::engineVersion` n'était jamais rempli (il l'est, depuis `config.h`), et `PluginContext::Engine()` restait nul pour tout plugin chargé avant le moteur, donc pour tous (`BindEngine` reçoit maintenant le moteur) : `get_asset` n'aurait jamais marché en vrai.

### Écarts par rapport au plan

* `CAbiPlugin` dérive d'`IModule` ; les rappels d'`EnginePlugin` et de `ToolPlugin` passent par des objets à part (voir A3).
* Les callbacks renvoient un `ShardResult` : une valeur non nulle fait échouer le plugin comme une exception en C++ (sauf `on_tool_task`, dont l'erreur annule la tâche).
* `ShardPluginInfo::structSize` est écrit par le plugin (son `sizeof`), pas par l'hôte : c'est ce qui permet de refuser une struct trop petite.
* Le lien `ShardPluginCtx` → contexte ne s'arrête plus dans `on_disable` mais quand le contexte libère tout (`PluginContext::OnRelease`, premier enregistrement donc dernier libéré) : `on_unload` peut encore libérer sa mémoire, les ressources d'un type d'asset sont rendues au plugin et ses blobs libérés tant que le contexte existe.
* Composants : des champs de données simples (nombres, booléen, vecteurs, quaternion, couleur) et des blobs. Pas de `STRING`, `ASSET`, `ENTITY`, `TAG`, donc pas de `construct` / `destroy` / `copy` ; les valeurs par défaut sont un bloc d'octets, pas du JSON. Drapeaux : `RUNTIME` (affiché en lecture seule, non sauvé) et `PRIVATE` ; pas de `HIDDEN` (l'inspecteur ne sait pas cacher un champ). Les noms sont préfixés par l'hôte (`<id>.<nom>`), comme les cvars.
* `shard.ecs` ne sert que depuis un système ; le fil principal passe par `shard.experimental.world_ecs`, sans promesse.
* Hooks : une seule struct `ShardHookArgs` (`subject`, `detail`, `frame`) décrite par un tableau dans l'en-tête, au lieu d'une struct par hook.
* Extensions : `add_validator`, `add_asset_type`, `add_importer` typées plutôt qu'un `add(point, name, item, item_type)` générique ; plus `get_asset` pour relire une ressource.
* Services : un service C est un `ShardService` (table + `self` + version) dont l'hôte garde la copie ; `resolve` refuse (`SHARD_E_UNSUPPORTED`) un service fourni par un plugin C++.
* La description d'un événement réutilise les `ShardFieldDesc` des composants (sans blob : une charge est copiée).
* La référence de l'API (`docs/docs/api_reference.md`) est générée depuis les commentaires des en-têtes par `scripts/gen_api_reference.py`, pas par Doxygen : le site est en MkDocs (du Markdown, pas le HTML de Doxygen), le script n'ajoute aucune dépendance, et il vérifie au passage que la révision annoncée par chaque en-tête est celle que l'hôte offre. Le test `Docs.ApiReference` échoue si la page n'est plus ce que donnent les en-têtes ; le workflow du site la régénère avant de construire.

### Reste

* Voir les étapes CI tourner : « autre compilateur » (Clang sous Linux, MSVC sous Windows) et l'archive SDK. Linux n'a jamais été vérifié sur cette machine.
* Mesurer le coût par appel sur `Spin` avec 100 000 entités contre le même système en C++ (risque du §11).
* Le critère de réception du §10 (le module Voxel contre le SDK seul) attend le module ; les blobs sont là pour lui.
* Le tutoriel sur une machine réellement vierge (sans MinGW ni moteur installés à côté).
* Candidats pour une 1.3 : faire passer `world_ecs` de l'expérimental au figé une fois éprouvé, champs `STRING` / `ASSET` / `ENTITY` (alloués par l'hôte, sur le modèle des blobs), écriture de `LocalTransform` qui remonte au `Transform`.
