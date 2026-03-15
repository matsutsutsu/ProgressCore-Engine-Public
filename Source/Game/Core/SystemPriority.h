#pragma once

namespace Priority {
    // ========================================================
    // Logic Group Stages (FixedUpdate)
    // 順番: 入力 -> AI/意思決定 -> 移動/アクション -> 物理更新 -> 衝突判定 -> 結果適用 -> 掃除
    // ========================================================
    enum class LogicStage {
        Input,      // 全ての入力・外部イベントの受付を待つ
        Update,     // AI、移動、弾の更新など。※ここはスケジューラが極限まで並列化する主戦場
        PrePhysics, // 空間分割（Grid）など、物理・衝突判定の準備
        Physics,    // 物理エンジン（積分）とTransform（親子関係）の更新
        Collision,  // 当たり判定（BroadPhase -> NarrowPhase）。Transformが確定している前提
        Resolution, // 衝突の解決など（めり込み押し出し、ダメージ計算、OnHitイベント）
        Cleanup     // 死亡エンティティの回収、メモリ整理
    };

    // ========================================================
    // Render Group Stages (Variable Update / Render)
    // 順番: カメラ決定 -> 環境設定 -> アニメーション -> 影 -> メイン描画 -> 画面効果 -> UI
    // ========================================================
    enum class RenderStage {
        Camera,      // CameraBrain, FreeCamera (描画用カメラ位置確定)
        Environment, // Light, Fog
        Animation,   // スケルタルメッシュ更新
        Shadow,      // シャドウマップ描画
        PreMain,     // 描画前の準備（エフェクトの更新、描画順のソートなど）
        Main,        // モデル、プリミティブ描画
        PostMain,    // モデル描画後の処理（エフェクトの描画コマンドなど）
        PostProcess, // ブルーム、トーンマップ
        UI           // ImGui, Sprite UI
    };
} // namespace Priority