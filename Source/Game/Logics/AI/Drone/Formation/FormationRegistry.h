// DroneFormationType → IFormation クラスを解決するレジストリ。
//        BTActionRegistry と同じ設計。switch を廃止する核。
//
// 1つの型に「C++ 実装」と「データ実装」を両方置けるようにしてある。
//   データ化が正しいかは、見た目が変わらないことでしか確かめられない。
//   実行中に差し替えられれば、同じ100機を同じ技で動かしたまま切り替えて
//   比較できる（変化が見えなければ再現できている）。
#pragma once
#include <array>
#include <memory>

#include "Formations_DataDriven.h"
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

    // 陣形タイプに C++ 実装を登録する。
    template <typename T>
    void Register(DroneFormationType type)
    {
        m_native[static_cast<size_t>(type)] = std::make_unique<T>();
    }

    // 陣形タイプにデータ実装（JSON 由来）を登録する。
    void RegisterSpec(DroneFormationType type, const FormationSpec &spec, const std::string &assetPath)
    {
        auto formation       = std::make_unique<DataDrivenFormation>();
        formation->spec      = spec;
        formation->assetPath = assetPath;
        m_data[static_cast<size_t>(type)] = std::move(formation);
    }

    // 実際に評価される陣形を引く。未登録なら nullptr。
    IFormation *Get(DroneFormationType type) const
    {
        const size_t i = static_cast<size_t>(type);
        if (m_preferData && m_data[i]) return m_data[i].get();
        return m_native[i].get();
    }

    bool Has(DroneFormationType type) const { return Get(type) != nullptr; }

    // 比較用。どちらの実装かを名指しで引く。
    IFormation          *GetNative(DroneFormationType type) const { return m_native[static_cast<size_t>(type)].get(); }
    DataDrivenFormation *GetData(DroneFormationType type) const { return m_data[static_cast<size_t>(type)].get(); }

    // データ実装を優先するか。両方ある型だけが影響を受ける。
    void SetPreferData(bool prefer) { m_preferData = prefer; }
    bool PreferData() const { return m_preferData; }

  private:
    FormationRegistry()                                     = default;
    FormationRegistry(const FormationRegistry &)            = delete;
    FormationRegistry &operator=(const FormationRegistry &) = delete;

    // DroneFormationType は uint8_t なので最大256個。密配列で高速アクセス。
    std::array<std::unique_ptr<IFormation>, 256>          m_native;
    std::array<std::unique_ptr<DataDrivenFormation>, 256> m_data;

    bool m_preferData = false;
};

} // namespace CCL::AI::Formation
