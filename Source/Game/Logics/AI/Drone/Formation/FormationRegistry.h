#pragma once
// ===========================================================================
// 概要: DroneFormationType から IFormation の実装を引くレジストリ（シングルトン）。
//
// 責務:
//   - 陣形の種類ごとに実装のインスタンスを1つ持つ
//   - 種類から実装を引く（未登録なら nullptr）
//   - データ陣形の表示名を返す
//
// 備考: BTActionRegistry と同じ設計で、陣形の分岐に switch を使わずに済ませる。
//       C++ の陣形を追加するときは Formations_*.h にクラスを書き、そのファイルの
//       RegisterAll() へ1行足す。データ陣形は JSON を置けば起動時に登録される。
// ===========================================================================
#include <array>
#include <memory>

#include "Game/Logics/AI/Drone/Components/DroneComponent.h"
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
            auto instance                           = std::make_unique<T>();
            T   *raw                                = instance.get();
            m_formations[static_cast<size_t>(type)] = std::move(instance);
            return raw;
        }

        // 実際に評価される陣形を引く。未登録なら nullptr。
        IFormation *Get(DroneFormationType type) const
        {
            return m_formations[static_cast<size_t>(type)].get();
        }

        bool Has(DroneFormationType type) const { return Get(type) != nullptr; }

        // データで書かれた技の名前（JSON の name）。C++ 実装の枠と空き枠は nullptr。
        // BTエディタが「データの技」の一覧と表示名を作るのに引く。
        const char *DataFormationName(DroneFormationType type) const
        {
            IFormation *formation = Get(type);
            return formation ? formation->DisplayName() : nullptr;
        }

    private:
        FormationRegistry()                                     = default;
        FormationRegistry(const FormationRegistry &)            = delete;
        FormationRegistry &operator=(const FormationRegistry &) = delete;

        // DroneFormationType は uint8_t なので最大256個。密配列にして添字で引く。
        std::array<std::unique_ptr<IFormation>, 256> m_formations;
    };

} // namespace CCL::AI::Formation
