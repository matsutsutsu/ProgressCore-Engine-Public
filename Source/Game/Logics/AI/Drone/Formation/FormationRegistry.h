/// @file  FormationRegistry.h
/// @brief DroneFormationType → IFormation クラスを解決するレジストリ。
///        BTActionRegistry と同じ設計。switch を廃止する核。
#pragma once
#include <array>
#include <memory>

#include "IFormation.h"
#include "Game/Logics/AI/Drone/DroneComponent.h"

namespace CCL::AI::Formation
{
class FormationRegistry
{
  public:
    static FormationRegistry &Instance()
    {
        static FormationRegistry reg;
        return reg;
    }

    /// @brief 陣形タイプにクラスを登録する。
    template <typename T>
    void Register(DroneFormationType type)
    {
        _formations[static_cast<size_t>(type)] = std::make_unique<T>();
    }

    /// @brief 陣形タイプから IFormation を引く。未登録なら nullptr。
    IFormation *Get(DroneFormationType type) const
    {
        return _formations[static_cast<size_t>(type)].get();
    }

    bool Has(DroneFormationType type) const
    {
        return _formations[static_cast<size_t>(type)] != nullptr;
    }

  private:
    FormationRegistry()                                     = default;
    FormationRegistry(const FormationRegistry &)            = delete;
    FormationRegistry &operator=(const FormationRegistry &) = delete;

    // DroneFormationType は uint8_t なので最大256個。密配列で高速アクセス。
    std::array<std::unique_ptr<IFormation>, 256> _formations;
};

} // namespace CCL::AI::Formation