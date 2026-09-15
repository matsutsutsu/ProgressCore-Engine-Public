// DroneFormationType → IFormation クラスを解決するレジストリ。
//        BTActionRegistry と同じ設計。switch を廃止する核。
//
// 陣形の実装は C++ クラス1つ＝1陣形。追加するときは Formations_*.h に
// クラスを書き、そのファイルの RegisterAll() へ1行足すだけでよい。
#pragma once
#include <array>
#include <memory>

#include "Game/Logics/AI/Drone/DroneComponent.h"
#include "IFormation.h"

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

    // 陣形タイプに実装を登録する。
    //
    // 生成したインスタンスを返すのは、データ陣形が「登録してからアセットを流し込む」
    // ためだけ。C++ 実装の陣形は既定構築で完成しているので戻り値を捨ててよい。
    template <typename T>
    T *Register(DroneFormationType type)
    {
        auto  instance = std::make_unique<T>();
        T    *raw      = instance.get();
        m_formations[static_cast<size_t>(type)] = std::move(instance);
        return raw;
    }

    // 実際に評価される陣形を引く。未登録なら nullptr。
    IFormation *Get(DroneFormationType type) const
    {
        return m_formations[static_cast<size_t>(type)].get();
    }

    bool Has(DroneFormationType type) const { return Get(type) != nullptr; }

  private:
    FormationRegistry()                                     = default;
    FormationRegistry(const FormationRegistry &)            = delete;
    FormationRegistry &operator=(const FormationRegistry &) = delete;

    // DroneFormationType は uint8_t なので最大256個。密配列で高速アクセス。
    std::array<std::unique_ptr<IFormation>, 256> m_formations;
};

} // namespace CCL::AI::Formation
