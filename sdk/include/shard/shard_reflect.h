/* Interface "shard.reflect" 1.0 : what exists in the engine and in the other plugins, described at run time ; and functions plugins offer to be
 * called by name.
 *
 * A tool that does not know the game when it is written needs this : a visual scripting editor lists the components, their fields, the events
 * and the functions it can wire together, a UI binds a label to "Health.current" of an actor, a console completes names, a network layer
 * walks the fields of a component to send them. The engine's own components ("light", "model"...), the components and scripts of every C
 * plugin, the events described with shard.events' `describe` and the functions below are all listed through the same calls.
 *
 * A FUNCTION is a named piece of code with a typed signature : "nav.set_destination(actor, vec3) -> bool". A plugin registers the ones it
 * wants a graph, a UI binding or a console to call ; anything with a signature that fits is called through function_call with ShardValues.
 * They are not a way for plugins to call each other in hot code (shard.services is) : a call converts its arguments to ShardValue and back.
 *
 * Names live in one flat space. A plugin prefixes its own with its id or a short name of its choice ("nav.set_destination") ; a function
 * another plugin already registered under that name is SHARD_E_ALREADY_EXISTS.
 *
 * thread : main, except where a function says otherwise. */
#ifndef SHARD_REFLECT_H
#define SHARD_REFLECT_H

#include "shard_abi.h"
#include "shard_components.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SHARD_IFACE_REFLECT "shard.reflect"

/* The types a value has. Fields of the engine's components are described with these too (more than shard.components' SHARD_FIELD_* can say). */
#define SHARD_VALUE_NONE    0u
#define SHARD_VALUE_BOOL    1u      /* ShardValue::i is 0 or 1 */
#define SHARD_VALUE_I32     2u      /* i */
#define SHARD_VALUE_I64     3u      /* i */
#define SHARD_VALUE_F32     4u      /* f[0] */
#define SHARD_VALUE_F64     5u      /* d */
#define SHARD_VALUE_VEC2    6u      /* f[0..1] */
#define SHARD_VALUE_VEC3    7u      /* f[0..2] */
#define SHARD_VALUE_VEC4    8u      /* f[0..3] */
#define SHARD_VALUE_QUAT    9u      /* f[0..3] : x, y, z, w */
#define SHARD_VALUE_COLOR   10u     /* f[0..3] : r, g, b, a */
#define SHARD_VALUE_STRING  11u     /* s : UTF-8 */
#define SHARD_VALUE_ACTOR   12u     /* i : a ShardActor */
#define SHARD_VALUE_ASSET   13u     /* i : an asset id (shard.assets), 0 for none */
#define SHARD_VALUE_JSON    14u     /* s : any value as JSON text (a list, a struct, what has no type of its own) */
#define SHARD_VALUE_ENUM    15u     /* s : the name of the chosen value */
#define SHARD_VALUE_ANY     16u     /* a parameter that takes any of the above (a "print" node) ; a value itself never has this type */

/* One value, flat so that its layout is plain. Which members are used is said above ; the others are ignored on input and zero on output. A
 * string is valid for the call it is passed to or returned from, and for a returned value until the same function is called again on this
 * thread. */
typedef struct ShardValue {
    uint32_t type;              /* SHARD_VALUE_* */
    uint32_t reserved;
    int64_t  i;
    double   d;
    float    f[4];
    ShardStr s;
} ShardValue;

/* ---- Types ---- */

#define SHARD_TYPE_COMPONENT    1u      /* a component of an actor : the engine's ("light"), or a plugin's ("com.acme.Spin", a script too) */
#define SHARD_TYPE_EVENT        2u      /* an event by name, with the payload shard.events' `describe` gave it */

typedef struct ShardTypeInfo {
    uint32_t structSize;
    uint32_t kind;              /* SHARD_TYPE_* */
    uint32_t fieldCount;
    uint32_t flags;             /* SHARD_TYPE_FLAG_* */
    ShardStr name;              /* valid while the type is registered */
    ShardStr plugin;            /* the plugin's id, empty for the engine's own */
    ShardStr category;          /* what the editor groups it under, may be empty */
} ShardTypeInfo;

#define SHARD_TYPE_FLAG_SCRIPT      1u  /* a component that is a script (shard.scripts) */
#define SHARD_TYPE_FLAG_ECS         2u  /* has an ECS side too (shard.components) : find_type finds it */

#define SHARD_FIELD_INFO_READONLY   1u  /* the field is shown but not saved or edited */
#define SHARD_FIELD_INFO_HIDDEN     2u

/* A field of a type : name, value type, and what an editor needs to show it. */
typedef struct ShardFieldInfo {
    uint32_t structSize;
    uint32_t valueType;         /* SHARD_VALUE_* */
    uint32_t flags;             /* SHARD_FIELD_INFO_* */
    uint32_t enumCount;         /* SHARD_VALUE_ENUM : how many values it has (field_enum_at) */
    float    min, max;          /* an editor's bounds ; equal : none */
    ShardStr name;              /* valid while the type is registered */
    ShardStr tooltip;
} ShardFieldInfo;

/* ---- Functions ---- */

#define SHARD_PARAM_OPTIONAL    1u      /* a call may leave it out (a trailing parameter only) */

typedef struct ShardParamDesc {
    uint32_t structSize;
    uint32_t type;              /* SHARD_VALUE_* */
    uint32_t flags;             /* SHARD_PARAM_* */
    uint32_t reserved;
    ShardStr name;
    ShardStr tooltip;           /* optional */
} ShardParamDesc;

#define SHARD_FUNCTION_PURE         1u  /* no side effect and the same result for the same arguments : a graph may skip, cache or reorder the call */
#define SHARD_FUNCTION_SIMULATION   2u  /* changes what the game simulates : only called from the simulation (a script's or a system's thread), never from an editor panel */
#define SHARD_FUNCTION_EDITOR_ONLY  4u  /* only meaningful in the editor (a bake, a selection) */

/* Runs the function. `args` has `argCount` values (never fewer than the required parameters) ; fill `results`, which has `resultCount` zeroed
 * values, and return SHARD_OK, or an error (SHARD_E_INVALID_ARG for a value the function cannot take). thread : the caller's. */
typedef ShardResult (SHARD_CALL *ShardFunctionFn)(void* user, ShardPluginCtx ctx, const ShardValue* args, uint32_t argCount, ShardValue* results, uint32_t resultCount);

typedef struct ShardFunctionDesc {
    uint32_t structSize;
    uint32_t flags;             /* SHARD_FUNCTION_* */
    uint32_t paramCount;
    uint32_t resultCount;
    ShardStr name;              /* "nav.set_destination" */
    ShardStr category;          /* "AI/Navigation" : how a palette groups it */
    ShardStr tooltip;
    const ShardParamDesc* params;       /* copied */
    const ShardParamDesc* results;      /* copied */
    ShardFunctionFn call;
    void* user;
} ShardFunctionDesc;

typedef struct ShardFunctionInfo {
    uint32_t structSize;
    uint32_t flags;
    uint32_t paramCount;
    uint32_t resultCount;
    ShardStr name;              /* strings valid while the function is registered */
    ShardStr category;
    ShardStr tooltip;
    ShardStr plugin;            /* the plugin that registered it */
} ShardFunctionInfo;

typedef struct ShardReflectApi {
    uint32_t structSize;

    /* ---- Types ---- */
    /* How many types of that kind (SHARD_TYPE_*) there are now, and the one at an index (stable while no plugin is loaded or unloaded). The
     * caller sets out->structSize. SHARD_E_NOT_FOUND : the index is out of range. thread : main */
    uint32_t    (SHARD_CALL *type_count)(uint32_t kind);
    ShardResult (SHARD_CALL *type_at)(uint32_t kind, uint32_t index, ShardTypeInfo* out);
    /* A type by its name ("light", "com.acme.Spin", "com.acme.RainStarted"). SHARD_E_NOT_FOUND. */
    ShardResult (SHARD_CALL *type_find)(uint32_t kind, ShardStr name, ShardTypeInfo* out);
    /* The fields of a type, in the order the inspector shows them. For an event : the fields `describe` gave. */
    ShardResult (SHARD_CALL *field_at)(uint32_t kind, ShardStr type, uint32_t index, ShardFieldInfo* out);
    /* The names a SHARD_VALUE_ENUM field takes, by index. */
    ShardResult (SHARD_CALL *field_enum_at)(uint32_t kind, ShardStr type, uint32_t field_index, uint32_t value_index, ShardStr* out);

    /* ---- Functions ---- */
    /* Registers a function, from on_load or later. It goes with the plugin. SHARD_E_INVALID_ARG : a bad name, a callback missing, an optional
     * parameter before a required one. */
    ShardResult (SHARD_CALL *function_add)(ShardPluginCtx ctx, const ShardFunctionDesc* desc);
    ShardResult (SHARD_CALL *function_remove)(ShardPluginCtx ctx, ShardStr name);
    uint32_t    (SHARD_CALL *function_count)(void);
    ShardResult (SHARD_CALL *function_at)(uint32_t index, ShardFunctionInfo* out);
    ShardResult (SHARD_CALL *function_find)(ShardStr name, ShardFunctionInfo* out);
    /* A parameter (result == SHARD_FALSE) or a result (SHARD_TRUE) of a function, by index. */
    ShardResult (SHARD_CALL *function_param_at)(ShardStr name, int32_t result, uint32_t index, ShardParamDesc* out);
    /* Calls it. The arguments are checked against the signature (a missing optional one is left out, a number is converted to the type asked,
     * anything else is SHARD_E_INVALID_ARG) ; `results` receives resultCount values. thread : the one the function is made for -- script or
     * main (the host answers SHARD_E_WRONG_THREAD otherwise), and not from a job. */
    ShardResult (SHARD_CALL *function_call)(ShardPluginCtx ctx, ShardStr name, const ShardValue* args, uint32_t arg_count, ShardValue* results, uint32_t result_count);
} ShardReflectApi;

#ifdef __cplusplus
}
#endif

#endif /* SHARD_REFLECT_H */
