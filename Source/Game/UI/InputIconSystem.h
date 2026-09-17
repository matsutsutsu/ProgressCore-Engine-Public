#pragma once
// InputIconComponent を持つ UIImage のアイコンを、今触っているデバイスへ合わせる。
//
// 設計方針:
//   - 「どのアイコンか」を決めるのはこのSystemだけ。操作説明・メニューの決定表示など
//     アイコンを出す画面が増えても、対応表はここ1箇所にしか無い。
//   - 毎フレーム解決し、変わったものだけ書き込む（差分書き込み）。持ち替え時だけ処理すると、
//     ポーズメニューのように後から生成されたアイコンが次の持ち替えまで空のままになる。
//   - デバイス判定そのものは Input が持つ（スティックのドリフト対策済み）。ここは使うだけ。
//
// 実行ステージ:
//   描画ステージ(R10_UI)に置く。ロジックステージだとポーズ中に走らないため、
//   ポーズ中に生成されたメニューのヒントアイコン（生成時 visible=false）が、
//   ポーズを解除するまで一度も解決されず出てこない
//   （ポーズ中は ECSContext::UpdatePaused が UpdateRender しか回さない）。
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

    // 同じ UIImageComponent を書く UIButtonSystem との前後を宣言する。
    //   宣言しないと TaskScheduler に「順序不定」として警告される
    //   （どちらが先かが並び順まかせになる）。
    //   収集System(R10b_UICollect)より前の帯なので、ここで差し替えた見た目は同じフレームで絵に出る。
    std::vector<const char *> GetRunAfter() const override { return {"UIButtonSystem"}; }

    void Update(float dt) override;

private:
    /**
     * @brief 割り当て(デバイス種別＋キーコード)をアイコンPNGのパスへ変換する。
     * @return 対応表に無ければ空文字（＝呼び出し側がその行を隠す）。
     */
    std::string ResolveBindingIcon(const InputBinding &binding, ActiveDeviceType device) const;

    // 割り当てに依らない固定アイコン（移動/カメラ/メニュー操作）を返す。
    std::string ResolveFixedIcon(InputIconKind kind, ActiveDeviceType device) const;
};
