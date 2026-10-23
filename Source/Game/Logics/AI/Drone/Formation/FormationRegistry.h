#pragma once
// ===========================================================================
// 概要: DroneFormationType から IFormation の実装を引くレジストリ（World の Resource）。
//
// 責務:
//   - 陣形の種類ごとに実装のインスタンスを1つ持つ
//   - 種類から実装を引く（未登録なら nullptr）
//   - データ陣形の表示名を返す
//
// 備考: C++ の陣形を追加するときは Formations_*.h にクラスを書き、そのファイルの RegisterAll() へ1行足す。
//       データ陣形は JSON を置けば起動時に登録される。
//
//       シングルトンにせず World ごとに持つ。中の陣形が World に依存する状態
//       （JSON のデータ・その World の小隊・エディタのプレビュー）を持つため。
//       シーンを切り替えると作り直されるので、Formation Editor で保存していない変更は消える。
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

        // 陣形の種類に実装を登録し、作った実体を返す（データ陣形が登録後にアセットを流し込むため）
        template <typename T>
        T *Register(DroneFormationType type)
        {
            auto instance                           = std::make_unique<T>();
            T   *raw                                = instance.get();
            m_formations[static_cast<size_t>(type)] = std::move(instance);
            return raw;
        }

        // 陣形を引く。未登録なら nullptr
        IFormation *Get(DroneFormationType type) const
        {
            return m_formations[static_cast<size_t>(type)].get();
        }

        bool Has(DroneFormationType type) const { return Get(type) != nullptr; }

        // 登録済みの全陣形の IFormation::BeginFrame を呼ぶ
        void BeginFrame(CCL::ECS::World &world)
        {
            for (auto &formation : m_formations)
                if (formation) formation->BeginFrame(world);
        }

        /**
         * @brief データで書かれた技の名前（JSON の name）を返す。BTエディタが表示名に使う。
         * @param type 調べる陣形の枠。
         * @return 技の名前。陣形が所有する。C++ 実装の枠と空き枠は nullptr。
         */
        const char *DataFormationName(DroneFormationType type) const
        {
            IFormation *formation = Get(type);
            return formation ? formation->DisplayName() : nullptr;
        }

    private:
        std::array<std::unique_ptr<IFormation>, 256> m_formations; // DroneFormationType の値を添字にした陣形の実体
    };

} // namespace CCL::AI::Formation
