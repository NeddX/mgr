#pragma once

#include <CommonDef.h>

#include <iostream>
#include <stdexcept>
#include <variant>

namespace pmgrd {
    namespace types {
        /*
        ** @brief Dummy struct for indicating that no errors occured.
        */
        struct Ok
        {
        };
    } // namespace types

    template <typename OkType, typename ErrType>
    struct ValuedResult
    {
    private:
        std::variant<OkType, ErrType> m_Value;

    public:
        constexpr ValuedResult(OkType okVal)
            : m_Value(std::move(okVal))
        {
        }
        constexpr ValuedResult(ErrType errVal)
            : m_Value(std::move(errVal))
        {
        }
        constexpr ValuedResult(const ValuedResult& other) noexcept
            : m_Value(other.m_Value)
        {
        }
        constexpr ValuedResult(ValuedResult&& other) noexcept
            : m_Value(std::move(other.m_Value))
        {
        }
        constexpr ValuedResult& operator=(const ValuedResult& other) noexcept
        {
            return ValuedResult{ other }.Swap(*this);
        }
        constexpr ValuedResult& operator=(ValuedResult&& other) noexcept
        {
            return ValuedResult{ std::move(other) }.Swap(*this);
        }

    public:
        [[nodiscard]] constexpr bool IsOk() const noexcept { return std::holds_alternative<OkType>(m_Value); }
        [[nodiscard]] constexpr bool IsErr() const noexcept { return std::holds_alternative<ErrType>(m_Value); }
        constexpr OkType             Unwrap() const
        {
            if (IsOk())
                return std::get<OkType>(std::move(m_Value));
            else
                throw std::runtime_error("Attempted to unwrap an Ok value.");
        }
        constexpr ErrType UnwrapErr() const
        {
            if (IsErr())
                return std::get<ErrType>(std::move(m_Value));
            else
                throw std::runtime_error("Attempted to unwrap an Err value.");
        }
        constexpr ValuedResult& Swap(ValuedResult& other) noexcept
        {
            std::swap(m_Value, other.m_Value);
            return *this;
        }

    public:
        [[nodiscard]] constexpr operator bool() const noexcept { return IsOk(); }

    public:
        [[nodiscard]] static constexpr ValuedResult Ok(OkType okType = OkType{}) noexcept
        {
            return ValuedResult{ std::forward<OkType>(okType) };
        }
        [[nodiscard]] static constexpr ValuedResult Err(ErrType errType = ErrType{}) noexcept
        {
            return ValuedResult{ std::forward<ErrType>(errType) };
        }
    };

    [[nodiscard]] static constexpr types::Ok Ok() noexcept
    {
        return types::Ok{};
    }

    template <typename ErrType>
    using Result = ValuedResult<types::Ok, ErrType>;
} // namespace pmgrd
