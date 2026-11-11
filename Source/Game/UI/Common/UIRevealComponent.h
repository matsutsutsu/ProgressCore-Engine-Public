#pragma once
#include "Engine/ECS/Common/CCL_Types.h"

#include <cstdint>
#include <string>

// 出方
enum class UIRevealMode : uint8_t
{
    Type,    // text を先頭から1文字ずつ打ち込む（UITextComponent が要る）
    SlideIn, // 左から減速しながら滑り込み、同時に濃くなる
    Grow,    // 左端を止めたまま、幅 0 から length まで伸びる（中心で置いた矩形用）
};

// UI の部品を演出つき（打ち込む・滑り込む・伸びる）で出すための設定と進み具合
// 進み具合を書くのは UIRevealSystem
struct UIRevealComponent
{
    UIRevealMode       mode     = UIRevealMode::Type;         // 出方
    float              delay    = 0.0f;                       // 出始めるまでの時間（秒、画面を出してから）
    float              duration = 0.35f;                      // SlideIn / Grow にかける時間（秒）
    float              speed    = 70.0f;                      // Type: 1秒に打ち込む文字数
    float              distance = 40.0f;                      // SlideIn: 左へずらして始める量（px）
    float              length   = 0.0f;                       // Grow: 伸びきったときの幅（px）
    std::string        text;                                  // Type: 打ち終わったときの文字列
    CCL::ECS::EntityID tip      = CCL::ECS::InvalidEntityID; // Grow: 伸びる先端についていく部品

    // --- UIRevealSystem が書く進み具合 ---
    float elapsed  = 0.0f;  // 画面を出してからの秒数
    bool  captured = false; // 置いた位置（baseX）を控えたか
    float baseX    = 0.0f;  // 置いた位置の offset.x（SlideIn は着地点、Grow は左端）
};
