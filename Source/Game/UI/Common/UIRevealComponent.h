#pragma once
// ===========================================================================
// 概要: UI の部品が「演出で出てくる」ための設定と進み具合を持つコンポーネント UIRevealComponent。
//
// 責務:
//   - 出方（文字を打ち込む・左から滑り込む・左から伸びる）と、その時間を持つ
//
// 備考: 動かすのは UIRevealSystem。リザルト・ポーズの左上の見出しの組み（ScreenHeadingUi.h）が使う。
//       描画ステージで動かすので、ポーズ中（ゲームの時間が止まっている間）も進む。
// ===========================================================================
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

struct UIRevealComponent
{
    UIRevealMode mode     = UIRevealMode::Type;
    float        delay    = 0.0f;  // 出始めるまでの秒数（画面を出してから）
    float        duration = 0.35f; // SlideIn / Grow にかける秒数
    float        speed    = 70.0f; // Type: 1秒に打ち込む文字数
    float        distance = 40.0f; // SlideIn: 左へずらして始める量（px）
    float        length   = 0.0f;  // Grow: 伸びきったときの幅（px）
    std::string  text;             // Type: 打ち終わったときの文字列

    // Grow: 伸びる先端についていく部品（線の右端の◆など）。無ければ InvalidEntityID
    CCL::ECS::EntityID tip = CCL::ECS::InvalidEntityID;

    // --- UIRevealSystem が書く進み具合 ---
    float elapsed  = 0.0f;  // 画面を出してからの秒数
    bool  captured = false; // 置いた位置（baseX）を控えたか
    float baseX    = 0.0f;  // 置いた位置の offset.x（SlideIn は着地点、Grow は左端）
};
