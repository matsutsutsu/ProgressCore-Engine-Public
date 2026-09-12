#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Physics/Components/RigidBody/JoltRigidbodyComponent.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Physics/Components/Jolt/JoltHandleComponent.h"

#include <Jolt/Physics/Collision/Shape/Shape.h> // ShapeRefC / ShapeSettings

// ===================================================================================
// ファイル名: JoltBodySetupSystem.h
// 役割:
// ECSの「設計図」から、Jolt空間に「実体」を生み出す全自動ファクトリーシステム。
//
// 【アーキテクチャ仕様】
// - 実行フェーズ: PrePhysics (物理計算の前)
// - 対象: Transform + Rigidbody + (各種Collider) を持ち、JoltHandle
// を持たないEntity。
//
// 【使い方・ルール】
// -
// 新しい形状（MeshColliderなど）を追加したい場合は、このシステムの分岐を増やす。
// - 生成が終わったEntityには JoltHandleComponent
// を付与し、二度とこの工場を通さないようにする。
// ===================================================================================


// 物理の「設計図」から「実体」を自動生成するファクトリーシステム
class JoltBodySetupSystem : public CCL::ECS::IfSystem<JoltBodySetupSystem,
                                CCL::ECS::Read<TransformComponent>,
                                CCL::ECS::Read<JoltRigidbodyComponent>> {
  public:
    JoltBodySetupSystem() : IfSystem("JoltBodySetupSystem") 
    {
		SetExclude<JoltHandleComponent>(); // JoltHandleComponent を持つ Entity は処理対象外
    }
    virtual ~JoltBodySetupSystem() = default;

    void Update(float dt) override;

  private:
    /**
     * @brief 形状を生成する唯一の窓口。Result を必ず検証してから取り出す。
     *
     * @details
     *  JPH::Result::Get() は中身が union で、エラー時は値が【構築されていない】。
     *      const Type & Get() const { JPH_ASSERT(IsValid()); return mResult; }
     *  つまり失敗した Result に対して Get() を呼ぶと、未初期化の ShapeRefC を
     *  コピーすることになり、そのコピーコンストラクタが野良ポインタに AddRef() する。
     *  JPH_ASSERT は Release/Development では消えるので、何の警告もなく壊れる。
     *
     *  特にメッシュコライダーはモデル次第で普通に失敗する（退化三角形・三角形数の上限）。
     *  そのため Create().Get() を直書きせず、必ずこの関数を通す。
     *
     * @return 失敗時は nullptr。呼び側の「shape が nullptr ならスキップ」に合流する。
     */
    JPH::ShapeRefC CreateShapeChecked(
        const JPH::ShapeSettings &settings, CCL::ECS::EntityID id, const char *what);
};