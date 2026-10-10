#pragma once

#include <cassert>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace Shard::Engine::Core {

    /// @brief Optional value : std::optional with the engine's vocabulary (Some / None), so that "there may be no
    /// value" reads the same as Result's "there may be an error".
    template <typename T>
    using Optional = std::optional<T>;

    inline constexpr std::nullopt_t None = std::nullopt;

    template <typename T>
    constexpr Optional<std::decay_t<T>> Some(T&& value) { return Optional<std::decay_t<T>>(std::forward<T>(value)); }

    /// @brief What a failed operation reports : a code for the program, a message for the human.
    struct Error {
        int code = 0;
        std::string message;

        Error() = default;
        Error(int code, std::string message) : code(code), message(std::move(message)) {}
        Error(std::string message) : message(std::move(message)) {}
        Error(const char* message) : message(message) {}
    };

    namespace Detail {
        template <typename T> struct OkWrapper { T value; };
        struct OkVoid {};
        template <typename E> struct ErrWrapper { E error; };
    }

    /// Success / failure markers : `return Ok(5);`, `return Err("file not found");`, `return Ok();`
    template <typename T>
    Detail::OkWrapper<std::decay_t<T>> Ok(T&& value) { return {std::forward<T>(value)}; }
    inline Detail::OkVoid Ok() { return {}; }
    template <typename E>
    Detail::ErrWrapper<std::decay_t<E>> Err(E&& error) { return {std::forward<E>(error)}; }
    inline Detail::ErrWrapper<Error> Err(const char* message) { return {Error(message)}; }
    inline Detail::ErrWrapper<Error> Err(int code, std::string message) { return {Error(code, std::move(message))}; }

    /// @brief Either a T (success) or an E (failure), for operations that can fail in ordinary ways (a file that is
    /// missing, a string that does not parse) without exceptions. The caller has to look at which one it got :
    ///
    ///     Result<Config> r = LoadConfig(path);
    ///     if (!r) { Log(r.GetError().message); return; }
    ///     Use(r.Value());
    ///
    /// Asking for the value of a failure (or the error of a success) is a bug, caught by assert in debug builds.
    template <typename T, typename E = Error>
    class Result {
    public:
        Result(Detail::OkWrapper<T>&& ok) : m_Storage(std::in_place_index<0>, std::move(ok.value)) {}
        template <typename U, typename = std::enable_if_t<std::is_convertible_v<U, T> && !std::is_same_v<U, T>>>
        Result(Detail::OkWrapper<U>&& ok) : m_Storage(std::in_place_index<0>, T(std::move(ok.value))) {}
        Result(Detail::ErrWrapper<E>&& err) : m_Storage(std::in_place_index<1>, std::move(err.error)) {}
        template <typename U, typename = std::enable_if_t<std::is_convertible_v<U, E> && !std::is_same_v<U, E>>>
        Result(Detail::ErrWrapper<U>&& err) : m_Storage(std::in_place_index<1>, E(std::move(err.error))) {}

        bool IsOk() const { return m_Storage.index() == 0; }
        bool IsErr() const { return m_Storage.index() == 1; }
        explicit operator bool() const { return IsOk(); }

        T& Value() & { assert(IsOk() && "Result::Value on an error"); return std::get<0>(m_Storage); }
        const T& Value() const& { assert(IsOk() && "Result::Value on an error"); return std::get<0>(m_Storage); }
        T&& Value() && { assert(IsOk() && "Result::Value on an error"); return std::get<0>(std::move(m_Storage)); }

        E& GetError() & { assert(IsErr() && "Result::GetError on a success"); return std::get<1>(m_Storage); }
        const E& GetError() const& { assert(IsErr() && "Result::GetError on a success"); return std::get<1>(m_Storage); }

        T ValueOr(T fallback) const& { return IsOk() ? std::get<0>(m_Storage) : std::move(fallback); }

        /// @brief Result<U> from f(value) on success, the same error on failure.
        template <typename F>
        auto Map(F&& f) const& -> Result<std::decay_t<decltype(f(std::declval<const T&>()))>, E> {
            using U = std::decay_t<decltype(f(std::declval<const T&>()))>;
            if (IsOk()) return Result<U, E>(Detail::OkWrapper<U>{f(std::get<0>(m_Storage))});
            return Result<U, E>(Detail::ErrWrapper<E>{std::get<1>(m_Storage)});
        }

        /// @brief f(value) (which itself returns a Result) on success, the same error on failure : chains
        /// operations that can each fail.
        template <typename F>
        auto AndThen(F&& f) const& -> decltype(f(std::declval<const T&>())) {
            using R = decltype(f(std::declval<const T&>()));
            if (IsOk()) return f(std::get<0>(m_Storage));
            return R(Detail::ErrWrapper<E>{std::get<1>(m_Storage)});
        }

    private:
        std::variant<T, E> m_Storage;
    };

    /// @brief Result without a value : "it worked" or "it failed".
    template <typename E>
    class Result<void, E> {
    public:
        Result(Detail::OkVoid) : m_Error(std::nullopt) {}
        Result(Detail::ErrWrapper<E>&& err) : m_Error(std::move(err.error)) {}
        template <typename U, typename = std::enable_if_t<std::is_convertible_v<U, E> && !std::is_same_v<U, E>>>
        Result(Detail::ErrWrapper<U>&& err) : m_Error(E(std::move(err.error))) {}

        bool IsOk() const { return !m_Error.has_value(); }
        bool IsErr() const { return m_Error.has_value(); }
        explicit operator bool() const { return IsOk(); }

        const E& GetError() const { assert(IsErr() && "Result::GetError on a success"); return *m_Error; }

    private:
        std::optional<E> m_Error;
    };
}
