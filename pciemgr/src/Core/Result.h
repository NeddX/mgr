#pragma once

#include <CommonDef.h>

#include <stdexcept>
#include <variant>

namespace pciemgr {
    /*
    ** @brief Dummy struct for indicating that no errors occured.
    */
    struct Ok
    {
    };

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

    public:
        [[nodiscard]] constexpr bool IsOk() const noexcept { return std::holds_alternative<OkType>(m_Value); }
        [[nodiscard]] constexpr bool IsErr() const noexcept { return std::holds_alternative<ErrType>(m_Value); }
        constexpr OkType             Unwrap() const
        {
            if (IsOk())
                return std::get<OkType>(m_Value);
            else
                throw std::runtime_error("Attempted to unwrap an Err value.");
        }
        constexpr ErrType UnwrapErr() const
        {
            if (IsErr())
                return std::get<ErrType>(m_Value);
            else
                throw std::runtime_error("Attempted to unwrap an Ok value.");
        }

    public:
        [[nodiscard]] constexpr operator bool() const noexcept { return IsOk(); }

    public:
        [[nodiscard]] static constexpr ValuedResult Ok(OkType okType = OkType{}) noexcept { return ValuedResult{ std::move(okType) }; }
        [[nodiscard]] static constexpr ValuedResult Err(ErrType errType = ErrType{}) noexcept
        {
            return ValuedResult{ std::move(errType) };
        }
    };

    template <typename ErrType>
    using Result = ValuedResult<Ok, ErrType>;
} // namespace pciemgr
