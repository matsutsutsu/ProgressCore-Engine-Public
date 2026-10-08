#pragma once
// ===========================================================================
// 概要: インスペクタのカテゴリ見出し名の定数。
//
// 責務:
//   - ComponentMeta の category に使う名前を 1 か所で定義する
//
// 備考:
//   - category はただの文字列なので、各所で直書きすると "Rendering" / "Render" のような
//     表記ゆれで見出しが 2 つに分かれる。必ずここの定数から選び、無ければここに追加する。
// @code
//   COMPONENT_META_BEGIN_FULL(HealthComponent, "Health", CCL::Categories::Gameplay, 100, false)
// @endcode
// ===========================================================================

namespace CCL::Categories
{

    // コア（常に上段に来るもの）
    inline constexpr const char *Transform = "Transform"; // 位置・回転・スケール・親子
    inline constexpr const char *Hierarchy = "Hierarchy"; // 親子リンク以外の階層情報

    // 表示
    inline constexpr const char *Rendering = "Rendering"; // Model, Mesh, Material, Renderer
    inline constexpr const char *Animation = "Animation"; // Animation, RootMotionなど
    inline constexpr const char *Camera    = "Camera";    // VirtualCamera, CameraBody 系
    inline constexpr const char *Light     = "Light";     // 各種ライト
    inline constexpr const char *Effect    = "Effect";    // パーティクル, VFX, シェーダーエフェクト

    // ゲームプレイ
    inline constexpr const char *Gameplay = "Gameplay"; // Health, Inventory, Stats 等の総称
    inline constexpr const char *Combat   = "Combat";   // 攻撃・ダメージ・武器
    inline constexpr const char *Movement = "Movement"; // 移動・入力連動の速度系
    inline constexpr const char *AI       = "AI";       // 敵AI・振る舞い木・ステートマシン

    // 物理
    inline constexpr const char *Physics = "Physics"; // Rigidbody, Collider, Joint

    // オーディオ
    inline constexpr const char *Audio = "Audio"; // 発音源・リスナー・ミキサー

    // UI・識別情報
    inline constexpr const char *UI   = "UI";   // 画面上のUI要素
    inline constexpr const char *Meta = "Meta"; // Name, Tag 等の識別・分類用データ

    // 内部・開発者向け
    inline constexpr const char *Internal = "Internal"; // DestroyTag 等、実行時専用の内部状態
    inline constexpr const char *Debug    = "Debug";    // デバッグ表示・診断専用

    // 分類が決まらない場合
    inline constexpr const char *Misc = "Misc"; // ComponentMeta<T> の既定値と同じ

} // namespace CCL::Categories