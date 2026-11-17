#pragma once
// ===========================================================================
// 概要: DroneFormationType から IFormation の実装を引くレジストリ（World の Resource）
//
// 責務:
//   - 陣形の種類ごとに実装のインスタンスを1つ持つ
//   - 種類から実装を引く（未登録なら nullptr）
//   - データ陣形の表示名を返す
//
// 備考: C++ の陣形は Formations_*.h にクラスを書き、その RegisterAll() へ1行足す。データ陣形は JSON を置けば登録される
//       中の陣形が World ごとの状態（JSON・小隊・エディタのプレビュー）を持つので、シングルトンにせず World ごとに持つ
//       シーンを切り替えると作り直されるので、Formation Editor で保存していない変更は消える
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

        // World に登録されているレジストリを返す。DroneFormationSystem::Initialize が作る前なら nullptr
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

        // データで書かれた技の名前（JSON の name）を返す。BT エディタが表示名に使う
        // 文字列は陣形が持つ。C++ 実装の枠と空き枠は nullptr
        const char *DataFormationName(DroneFormationType type) const
        {
            IFormation *formation = Get(type);
            return formation ? formation->DisplayName() : nullptr;
        }

    private:
        std::array<std::unique_ptr<IFormation>, 256> m_formations; // DroneFormationType の値を添字にした陣形の実体
    };

}
