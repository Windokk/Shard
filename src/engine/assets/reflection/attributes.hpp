#pragma once

#define SHARD_COMMA_ARGS(...) , ##__VA_ARGS__
#define SHARD_COMMA_ARGS_SP(...) , ##__VA_ARGS__  // helper for space

#if defined(__clang__) && !defined(FIELD)
    #define FIELD(...) __attribute__((annotate("field ," #__VA_ARGS__)))
#elif !defined(FIELD)
    #define FIELD(...) [[maybe_unused]]
#endif

#if defined(__clang__) && !defined(CLASS)
    #define CLASS(...) __attribute__((annotate("class" #__VA_ARGS__)))
#elif !defined(CLASS)
    #define CLASS(...) [[maybe_unused]]
#endif

#if defined(__clang__) && !defined(STRUCT)
    #define STRUCT(...) __attribute__((annotate("struct" #__VA_ARGS__)))
#elif !defined(STRUCT)
    #define STRUCT(...) [[maybe_unused]]
#endif

#define DECLARE_DESCRIPTOR(Type)           \
public:                                              \
    static ClassDescriptor descriptor;           \
    const ClassDescriptor* GetDescriptor() const override { \
        return &Type::descriptor;                    \
    }
