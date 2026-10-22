#pragma once
// ===========================================================================
// 概要: DroneFormationType から IFormation の実装を引くレジストリ（World の Resource）。
//
// 責務:
//   - 陣形の種類ごとに実装のインスタンスを1つ持つ
//   - 種類から実装を引く（未登録なら nullptr）
//   - データ陣形の表示名を返す
//
// 備考: BTActionRegistry と同じ設計で、陣形の分岐に switch を使わずに済ませる。
//       C++ の陣形を追加するときは Formations_*.h にクラスを書き、そのファイルの
//       RegisterAll() へ1行足す。データ陣形は JSON を置けば起動時に登録される。
//
//       BTActionRegistry と違ってシングルトンにせず、World ごとに持つ。
//       中の陣形が World に依存する状態を持つため（JSON から読んだデータ、BeginFrame で取る
//       その World の小隊、エディタのプレビュー）。作るのは DroneFormationSystem::Initialize。
//
//       シーンを切り替えると作り直されるので、Formation Editor で直して保存していない中身は消える。
// ===========================================================================
#include <array>
#include <memory>

#include "Game/Logics/AI/Drone/Components/DroneComponent.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "IFormation.h"

namespace CCL::AI::Formation
{
    class FormationRegistry
    {
    public:
        FormationRegistry()                                     = default;
        FormationRegistry(const FormationRegistry &)            = delete;
        FormationRegistry &operator=(const FormationRegistry &) = delete;

        /**
         * @brief World に登録されているレジストリを返す。
         * @param world 探す World。
         * @return レジストリ。DroneFormationSystem::Initialize が作る前なら nullptr。
         */
        static FormationRegistry *Find(CCL::ECS::World &world)
        {
            return world.HasResource<FormationRegistry>() ? &world.GetResource<FormationRegistry>() : nullptr;
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

        // 登録済みの全陣形に、フレームの始まりを伝える（IFormation::BeginFrame）。
        void BeginFrame(CCL::ECS::World &world)
        {
            for (auto &formation : m_formations)
                if (formation) formation->BeginFrame(world);
        }

        /**
         * @brief データで書かれた技の名前（JSON の name）を返す。
         *
         * BTエディタが「データの技」の一覧と表示名を作るのに引く。
         *
         * @param type 調べる陣形の枠。
         * @return 技の名前。陣形が所有する。C++ 実装の枠と空き枠は nullptr。
         */
        const char *DataFormationName(DroneFormationType type) const
        {
            IFormation *formation = Get(type);
            return formation ? formation->DisplayName() : nullptr;
        }

    private:
        // DroneFormationType は uint8_t なので最大256個。密配列にして添字で引く。
        std::array<std::unique_ptr<IFormation>, 256> m_formations;
    };

} // namespace CCL::AI::Formation
