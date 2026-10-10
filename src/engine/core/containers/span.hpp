#pragma once

#include <array>
#include <cassert>
#include <cstddef>
#include <iterator>
#include <type_traits>
#include <vector>

namespace Shard::Engine::Core {

    /// @brief A non-owning view of `size` contiguous Ts (pointer + length), the C++17 stand-in for std::span.
    /// Pass it instead of (const T*, size_t) or `const std::vector<T>&` : it accepts arrays, vectors, std::array and
    /// sub-ranges alike, and it never copies. The viewed memory must outlive the span.
    template <typename T>
    class Span {
    public:
        using element_type = T;
        using value_type = std::remove_cv_t<T>;
        using iterator = T*;
        using reverse_iterator = std::reverse_iterator<T*>;

        constexpr Span() noexcept = default;
        constexpr Span(T* data, size_t size) noexcept : m_Data(data), m_Size(size) {}
        constexpr Span(T* first, T* last) noexcept : m_Data(first), m_Size(static_cast<size_t>(last - first)) {}

        template <size_t N>
        constexpr Span(T (&array)[N]) noexcept : m_Data(array), m_Size(N) {}

        template <typename U, size_t N, typename = std::enable_if_t<std::is_convertible_v<U (*)[], T (*)[]>>>
        constexpr Span(std::array<U, N>& array) noexcept : m_Data(array.data()), m_Size(N) {}

        template <typename U, size_t N, typename = std::enable_if_t<std::is_convertible_v<const U (*)[], T (*)[]>>>
        constexpr Span(const std::array<U, N>& array) noexcept : m_Data(array.data()), m_Size(N) {}

        template <typename U, typename A, typename = std::enable_if_t<std::is_convertible_v<U (*)[], T (*)[]>>>
        Span(std::vector<U, A>& vector) noexcept : m_Data(vector.data()), m_Size(vector.size()) {}

        template <typename U, typename A, typename = std::enable_if_t<std::is_convertible_v<const U (*)[], T (*)[]>>>
        Span(const std::vector<U, A>& vector) noexcept : m_Data(vector.data()), m_Size(vector.size()) {}

        /// Span<T> -> Span<const T>
        template <typename U, typename = std::enable_if_t<std::is_convertible_v<U (*)[], T (*)[]> && !std::is_same_v<U, T>>>
        constexpr Span(const Span<U>& other) noexcept : m_Data(other.data()), m_Size(other.size()) {}

        constexpr T* data() const noexcept { return m_Data; }
        constexpr size_t size() const noexcept { return m_Size; }
        constexpr size_t size_bytes() const noexcept { return m_Size * sizeof(T); }
        constexpr bool empty() const noexcept { return m_Size == 0; }

        constexpr T* begin() const noexcept { return m_Data; }
        constexpr T* end() const noexcept { return m_Data + m_Size; }
        reverse_iterator rbegin() const noexcept { return reverse_iterator(end()); }
        reverse_iterator rend() const noexcept { return reverse_iterator(begin()); }

        constexpr T& operator[](size_t i) const { assert(i < m_Size && "Span index out of range"); return m_Data[i]; }
        constexpr T& front() const { assert(m_Size > 0); return m_Data[0]; }
        constexpr T& back() const { assert(m_Size > 0); return m_Data[m_Size - 1]; }

        /// @brief The first `count` elements.
        constexpr Span first(size_t count) const { assert(count <= m_Size); return Span(m_Data, count); }
        /// @brief The last `count` elements.
        constexpr Span last(size_t count) const { assert(count <= m_Size); return Span(m_Data + (m_Size - count), count); }
        /// @brief `count` elements from `offset` (count defaults to "to the end").
        constexpr Span subspan(size_t offset, size_t count = npos) const {
            assert(offset <= m_Size);
            const size_t available = m_Size - offset;
            return Span(m_Data + offset, count == npos ? available : (count < available ? count : available));
        }

        static constexpr size_t npos = static_cast<size_t>(-1);

    private:
        T* m_Data = nullptr;
        size_t m_Size = 0;
    };

    template <typename T>
    Span<T> MakeSpan(T* data, size_t size) { return Span<T>(data, size); }
    template <typename T>
    Span<T> MakeSpan(std::vector<T>& v) { return Span<T>(v); }
    template <typename T>
    Span<const T> MakeSpan(const std::vector<T>& v) { return Span<const T>(v); }

    /// @brief The bytes of a span, for serialisation or uploads.
    template <typename T>
    Span<const unsigned char> AsBytes(Span<T> span) {
        return Span<const unsigned char>(reinterpret_cast<const unsigned char*>(span.data()), span.size_bytes());
    }
}
