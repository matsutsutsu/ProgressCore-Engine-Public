#pragma once
// システムの実行順序を定義するヘッダーファイル

// 途中から追加したくなったらL01a などとすれば、IntelliSense上で順番を保ったまま追加できる

namespace Priority {
    // ========================================================
    // Logic Group Stages (FixedUpdate)
    // 接頭辞(L01〜)により、IntelliSense上で実行順にソートされます。
    // ========================================================
    enum class LogicStage : int {
        L01_Input = 100, // 全ての入力・外部イベントの受付

        // 時間管理フェーズ
        L01_TimeSetup = 110, // 動的なTimeStateの付与 (TimeScaleSetupSystem)
        L01_TimeScale = 120, // 全エンティティの実効dtの計算 (TimeScaleSystem)

        // 世界の事実の集計フェーズ
        L01_WorldState = 130, // 「誰から見ても同じ値」を確定させる (BattleContextSyncSystem)
                              //   AI層(L02〜)が読むより厳密に前でなければならない。
                              //   ※ L02_PreUpdate に置いてはいけない: DroneBrainSystem が
                              //     同ステージで BattleContext を読むため、書き手と読み手が
                              //     同居して順序が決まらなくなる。しかもリソースへのアクセスは
                              //     IfSystem の Read<>/Write<> 申告に載らないので、
                              //     スケジューラはこの依存を検出できない。

		L02_PreUpdate = 150, // AIの感知フェーズ。L02_Update の入力を確定させるものをここへ。
                             //   例: 知覚(BossPerceptionSystem) / 大状態(BossPhaseSystem) /
                             //       個体判断(DroneBrainSystem)

        L02_Update = 200, // AI、移動、弾の更新など (並列化の主戦場)

        L02_PostUpdate = 250,  // すべてのUpdate系システムの後に実行される、主にアニメーションの更新と物理への反映をここで行う

        L03_PrePhysics = 300, // 空間分割(Grid)・ボーン追従(BoneAttachment)など、物理・衝突判定の準備
        L03_PushToPhysics = 350, // アニメ/追従で確定した Transform を Jolt の Kinematic 剛体へ押し込む。
                                 //   必ず BoneAttachment(L03_PrePhysics) の後・物理ステップ(L04)の前に置く。
                                 //   ※ここが BoneAttachment より前だと、剣ボディが振りに追従せず当たらない。
        L04_Physics = 400, // 物理エンジン(積分)とTransform(親子関係)の更新
        L05_Collision = 500, // 当たり判定 (Broad -> Narrow)

        L06_Resolution = 600, // 衝突の解決 (めり込み押し出し、ダメージ計算)
        L06_DamageApply = 610, // ① ダメージイベントの消費とHP減算 (DamageResolutionSystem)
        L06_StatusEffect = 620, // ② 毒やバフの適用 (将来用)
        L06_HealthManagement = 630, // ③ HPの最終確認と死亡判定 (HealthSystem)
        L06_HudSync = 650, // ④ 確定したHP/ゲージ等をHUDへ反映。
                           //   必ず HealthManagement(630) の後に置く。
                           //   ※ここより前だと HUD が常に1フレーム前の値を表示する。

        L07_Cleanup = 700,  // 死亡エンティティの回収、メモリ整理
        L08_Prefab = 800,  // プレハブ生成
    };

    // ========================================================
    // Render Group Stages (Variable Update / Render)
    // 接頭辞(R01〜)により、IntelliSense上で実行順にソートされます。
    // カリングシステム(R04, R05)も明確な位置に配置しています。
    // ========================================================
    enum class RenderStage : int {
        R00_Interpolation = 50, // 補間済みワールド行列の確定（カメラより前）

        R00b_CameraTargetBind = 75, // カメラの追従/注視 target を役割で解決（必ずカメラ本体の前）

        R01_Camera = 100, // CameraBrain, FreeCamera (カメラ位置確定)

        R01b_CameraBrain = 150, // vCamを集約しMainCameraへ反映（必ずBodyの後）

        R02_Environment = 200, // Light, Fogの設定
        R03_Animation = 300, // スケルタルメッシュ更新 (頂点が動く)
        R04_BoundsUpdate = 400, // TransformやAnimから正確なAABBを再計算
        R05_Culling = 500, // カメラ視錐台とAABBの交差判定 (フラグON/OFF)
        R06_Shadow = 600, // シャドウマップ描画
        R07_PreMain = 700, // 描画前準備
        R08_Main = 800, // Model, Primitive などのメイン描画
        R09_PostProcess = 900, // Bloom, ToneMapping などの画面効果
        R10_UI = 1000 // ImGui, UI描画
    };
}