// =====================================================================
// ComponentCategories.h
//   インスペクタのカテゴリ見出し名を一元管理する定数集。
//
//   目的:
//     category はただの const char* なので、放っておくと
//     "Rendering" / "Render" / "描画" のような表記ゆれや、
//     似た概念のカテゴリが乱立してインスペクタが再び雑然とする。
//     必ずこのファイルの定数から選ぶ (無ければ追加する) 運用にすることで、
//     文字列比較で一致しないと2つの見出しに分裂する事故を防ぐ。
//
//   使い方:
//     #include "Engine/Serialization/Meta/ComponentCategories.h"
//     COMPONENT_META_BEGIN_FULL(HealthComponent, "Health",
//         CCL::Categories::Gameplay, 100, false)
//
//   ★ 新しいカテゴリが必要になったら、まず本ファイルに追記すること。
//     コンポーネント側の .cpp に生文字列でカテゴリを増やすのは禁止。
//     追加ルールは Meta 実装マニュアル 3-2 節を参照。
// =====================================================================
#pragma once

namespace CCL::Categories {

    // --- コア / 常に上段に来るもの ---------------------------------
    inline constexpr const char* Transform = "Transform";   // 位置・回転・スケール・親子
    inline constexpr const char* Hierarchy = "Hierarchy";   // 親子リンク以外の階層情報

    // --- 表示系 ------------------------------------------------------
    inline constexpr const char* Rendering = "Rendering";   // Model, Mesh, Material, Renderer
    inline constexpr const char* Animation = "Animation";   // Animation, RootMotionなど
    inline constexpr const char* Camera    = "Camera";      // VirtualCamera, CameraBody 系
    inline constexpr const char* Light     = "Light";       // 各種ライト
    inline constexpr const char* Effect    = "Effect";      // パーティクル, VFX, シェーダーエフェクト

    // --- ゲームプレイ --------------------------------------------------
    inline constexpr const char* Gameplay  = "Gameplay";    // Health, Inventory, Stats 等の総称
    inline constexpr const char* Combat    = "Combat";      // 攻撃・ダメージ・武器
    inline constexpr const char* Movement  = "Movement";    // 移動・入力連動の速度系
    inline constexpr const char* AI        = "AI";          // 敵AI・振る舞い木・ステートマシン

    // --- 物理 ----------------------------------------------------------
    inline constexpr const char* Physics   = "Physics";     // Rigidbody, Collider, Joint

    // --- オーディオ ------------------------------------------------------
    inline constexpr const char* Audio     = "Audio";       // 発音源・リスナー・ミキサー

    // --- UI / メタ情報 ---------------------------------------------------
    inline constexpr const char* UI        = "UI";          // 画面上のUI要素
    inline constexpr const char* Meta      = "Meta";        // Name, Tag 等の識別・分類用データ

    // --- 内部 / 開発者向け -------------------------------------------------
    inline constexpr const char* Internal  = "Internal";    // DestroyTag 等、実行時専用の内部状態
    inline constexpr const char* Debug     = "Debug";       // デバッグ表示・診断専用

    // --- 分類が定まらない場合の既定値 ------------------------------------
    inline constexpr const char* Misc      = "Misc";        // ComponentMeta<T> の既定値と同じ

} // namespace CCL::Categories