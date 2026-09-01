#pragma once
/// @file InputIconSystem.h
/// @brief InputIconComponent を持つ UIImage のアイコンを、今触っているデバイスへ合わせる。
///
/// 設計方針:
///   - 「どのアイコンか」を決めるのはこのSystemだけ。操作説明・メニューの決定表示など
///     アイコンを出す画面が増えても、対応表はここ1箇所にしか無い。
///   - 毎フレーム解決し、変わったものだけ書き込む（差分書き込み）。持ち替え時だけ処理すると、
///     ポーズメニューのように後から生成されたアイコンが次の持ち替えまで空のままになる。
///   - デバイス判定そのものは Input が持つ（スティックのドリフト対策済み）。ここは使うだけ。
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Platform/Input/IInputAPI.h"
#include "Engine/UI/ECS/Components/UIImageComponent.h"
#include "Game/UI/InputIconComponent.h"

#include <string>

class InputIconSystem : public CCL::ECS::IfSystem<InputIconSystem,
                            CCL::ECS::Write<UIImageComponent>,
                            CCL::ECS::Read<InputIconComponent>>
{
  public:
    InputIconSystem()
        : IfSystem("InputIconSystem")
    {
    }

    void Update(float dt) override;

  private:
    /// @brief 割り当て(デバイス種別＋キーコード)をアイコンPNGのパスへ変換する。
    /// @return 対応表に無ければ空文字（＝呼び出し側がその行を隠す）。
    std::string ResolveBindingIcon(const InputBinding &binding, ActiveDeviceType device) const;

    /// @brief 割り当てに依らない固定アイコン（移動/カメラ/メニュー操作）を返す。
    std::string ResolveFixedIcon(InputIconKind kind, ActiveDeviceType device) const;
};
