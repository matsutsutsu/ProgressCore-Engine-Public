#pragma once
// System の実行ステージ。Logic（FixedUpdate）と Render（毎フレーム）のステージ値を定める
// 値が小さいほど先に走る。ステージが違えば必ず直列、同じなら並列の候補になる
// 途中に足すときは L01a / R00b のように接尾辞を付けると、IntelliSense で実行順に並ぶ

namespace Priority
{
    // ============================================================
    // Logic グループ（FixedUpdate）。接頭辞 L01〜 で実行順に並ぶ
    // ============================================================
    enum class LogicStage : int
    {
        L01_Input = 100, // すべての入力・外部イベントの受付

        // 時間管理フェーズ
        L01_TimeSetup = 110, // TimeState の自動付与（TimeScaleSetupSystem）
        L01_TimeScale = 120, // 全 Entity の実効 dt の計算（TimeScaleSystem）

        // 世界の事実の集計フェーズ
        L01_WorldState = 130, // 「誰から見ても同じ値」を確定させる（BattleContextSyncSystem）
                              //   AI層(L02〜)が読むより厳密に前でなければならない
                              //   ※ L02_PreUpdate に置いてはいけない: DroneBrainSystem が
                              //     同ステージで BattleContext を読むため、書き手と読み手が
                              //     同居して順序が決まらなくなる。しかもリソースへのアクセスは
                              //     IfSystem の Read<>/Write<> 申告に載らないので、
                              //     スケジューラはこの依存を検出できない

        L02_PreUpdate = 150, // AI の感知フェーズ。L02_Update の入力を確定させるものをここへ
                             //   例: 知覚（BossPerceptionSystem）/ 形態（BossPhaseSystem）/
                             //       個体判断（DroneBrainSystem）

        L02_Update = 200, // AI・移動・弾の更新など（System 数が最も多く、並列化の効果が大きい）

        L02_PostUpdate = 250, // 全 Update 系の後。主にアニメーションの更新と物理への反映

        L03_PrePhysics    = 300, // 空間分割（Grid）・ボーン追従（BoneAttachment）など、物理・衝突判定の準備
        L03_PushToPhysics = 350, // アニメ/追従で確定した Transform を Jolt の Kinematic 剛体へ押し込む
                                 //   必ず BoneAttachment(L03_PrePhysics) の後・物理ステップ(L04)の前に置く
                                 //   ※ここが BoneAttachment より前だと、剣ボディが振りに追従せず当たらない
        L04_Physics   = 400,     // 物理エンジン（積分）と Transform（親子関係）の更新
        L05_Collision = 500,     // 当たり判定（Broad -> Narrow）

        L06_Resolution       = 600, // 衝突の解決（めり込み押し出し、ダメージ計算）
        L06_DamageApply      = 610, // ダメージイベントの消費と HP 減算（DamageResolutionSystem）
        L06_StatusEffect     = 620, // 毒やバフの適用（現在この段に System はない）
        L06_HealthManagement = 630, // HP の最終確認と死亡判定（HealthSystem）
        L06_HudSync          = 650, // 確定した HP/ゲージ等を HUD へ反映
                                    //   必ず HealthManagement(630) の後に置く
                                    //   ※ここより前だと HUD が常に1フレーム前の値を表示する

        L07_Cleanup = 700, // 死亡 Entity の回収、メモリ整理
        L08_Prefab  = 800, // プレハブ生成
    };

    // ============================================================
    // Render グループ（毎フレーム）。接頭辞 R00〜 で実行順に並ぶ
    // ============================================================
    enum class RenderStage : int
    {
        R00_Interpolation = 50, // 補間済みワールド行列の確定（カメラより前）

        R00b_CameraTargetBind = 75, // カメラの追従/注視 target を役割で解決（必ずカメラ本体の前）

        R01_Camera = 100, // CameraBrain, FreeCamera（カメラ位置確定）

        R01b_CameraBrain = 150, // vCam を集約し MainCamera へ反映（必ず Body の後）

        R02_Environment = 200, // Light, Fog の設定

        R02b_SkeletalPose = 250, // 骨格ポーズの確定（AnimationPoseSystem）
                                 //   ロジックは1描画フレームに最大4回走るが、ポーズを行列にするのは R03_Animation の1回だけなので、
                                 //   ロジックで求めると最後の1回以外が捨てられる。時間とイベントは AnimationSystem がロジックで進める

        R03_Animation    = 300,  // スケルタルメッシュ更新（頂点が動く）
        R04_BoundsUpdate = 400,  // Transform やアニメから正確な AABB を再計算
        R05_Culling      = 500,  // カメラ視錐台と AABB の交差判定（可視フラグの ON/OFF）
        R06_Shadow       = 600,  // シャドウマップ描画
        R07_PreMain      = 700,  // 描画前準備
        R08_Main         = 800,  // Model, Primitive などのメイン描画
        R09_PostProcess  = 900,  // Bloom, ToneMapping などの画面効果
        R10_UI = 1000, // UI の「状態」を決める帯
                       //   入力判定 → ボタンの見た目/アイコン差し替え → 追従アニメ/出現演出 →
                       //   矩形(computedPos/Size)の確定、までをここで終わらせる

        R10b_UICollect = 1050, // UI の描画リスト（UIDrawList）を積む帯。画像・文字・ゲージの収集 System を置く
                               //   R10_UI と分けると、ステージの境目で必ず直列になるので、収集はいつも状態が決まった後に走る
                               //   同じステージだと組み合わせの数だけ GetRunAfter が要り、漏れると色や hover の拡大が 1〜2 フレーム遅れる

        R11_AudioTick = 1100 // オーディオエンジンの1フレーム更新（AudioEngineTickSystem）
                             //   このフレームで決まった発音・リスナー位置・スロー度を
                             //   すべて反映し終えた後に1回だけ叩きたいので、Render の最後段
    };
}