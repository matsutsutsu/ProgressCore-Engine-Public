#pragma once
// ===========================================================================
// 概要: InputIconComponent を持つ UIImage のアイコンを、今使っているデバイスに合わせる System
//
// 責務:
//   - 割り当てや固定の種類から、アイコンの画像のパスを決める
//   - パスが変わったときだけ書き換え、アイコンがなければ隠す
//
// 備考: アイコンの対応表はここにしかない
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Platform/Input/IInputAPI.h"
#include "Engine/UI/ECS/Components/UIImageComponent.h"
#include "Game/UI/Input/InputIconComponent.h"

#include <string>
#include <vector>

class InputIconSystem : public CCL::ECS::IfSystem<
    InputIconSystem,
    CCL::ECS::Write<UIImageComponent>,
    CCL::ECS::Read<InputIconComponent>>
{
public:
    InputIconSystem() : IfSystem("InputIconSystem") {}

    // 同じ UIImageComponent を書く UIButtonSystem の後に動かす
    // 決めないと順番が登録順で変わり、TaskScheduler が順序不定の警告を出す
    std::vector<const char *> GetRunAfter() const override { return {"UIButtonSystem"}; }

    void Update(float dt) override;

private:
    // 割り当て（デバイスの種類とキーコード）をアイコンのパスに変える（対応表になければ空）
    std::string ResolveBindingIcon(const InputBinding &binding, ActiveDeviceType device) const;

    // 割り当てに依らない固定のアイコン（移動・カメラ・メニューの操作）のパスを返す
    std::string ResolveFixedIcon(InputIconKind kind, ActiveDeviceType device) const;
};
