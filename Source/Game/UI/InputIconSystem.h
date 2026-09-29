#pragma once
// ===========================================================================
// 概要: InputIconComponent を持つ UIImage のアイコンを、今使っているデバイスに合わせる System。
//
// 責務:
//   - 割り当て（デバイスの種類とキーコード）や固定の種類から、アイコンの画像のパスを決める
//   - パスが変わったときだけ texturePath を書き換え、アイコンが無ければ隠す
//
// 備考: どのアイコンを出すかを決めるのはこの System だけで、対応表はここにしか無い。
//       デバイスの判定は Input が行う（スティックのドリフトも考慮済み）。
//       毎フレーム決め直すのは、デバイスを持ち替えたときだけにすると、後から作られたアイコン
//       （ポーズメニューのヒントなど）が次に持ち替えるまで空のままになるため。
//       描画ステージ（R10_UI）に置くのも同じ理由。ポーズ中は ECSContext::UpdatePaused が描画の
//       System しか回さないので、ロジックステージだとポーズ中に作ったヒントが出てこない。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Platform/Input/IInputAPI.h"
#include "Engine/UI/ECS/Components/UIImageComponent.h"
#include "Game/UI/InputIconComponent.h"

#include <string>
#include <vector>

class InputIconSystem : public CCL::ECS::IfSystem<InputIconSystem,
                                                  CCL::ECS::Write<UIImageComponent>,
                                                  CCL::ECS::Read<InputIconComponent>>
{
public:
    InputIconSystem()
        : IfSystem("InputIconSystem")
    {
    }

    // 同じ UIImageComponent を書く UIButtonSystem との順番を決める。
    // 決めないと、どちらが先かが登録順で変わり、TaskScheduler が順序不定の警告を出す。
    // 描画用に集める System（R10b_UICollect）より前なので、ここで変えた見た目は同じフレームで出る
    std::vector<const char *> GetRunAfter() const override { return {"UIButtonSystem"}; }

    void Update(float dt) override;

private:
    /**
     * @brief 割り当て（デバイスの種類とキーコード）を、アイコンの PNG のパスに変える。
     * @return 対応表に無ければ空の文字列（呼び出し側がそのアイコンを隠す）
     */
    std::string ResolveBindingIcon(const InputBinding &binding, ActiveDeviceType device) const;

    // 割り当てに依らない固定のアイコン（移動・カメラ・メニューの操作）のパスを返す
    std::string ResolveFixedIcon(InputIconKind kind, ActiveDeviceType device) const;
};
