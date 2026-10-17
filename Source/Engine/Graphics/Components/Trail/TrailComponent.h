#pragma once
// ===========================================================================
// 概要: 剣など振る武器の軌跡（ソードトレイル）を残すコンポーネント。
//
// 責務:
//   - 刃のローカルな2端点（根本・先端）と、見た目のパラメータを持つ
//   - 過去の断面（Segment）を固定長のリングバッファに持つ
//   - 一時的にトレイルを出すためのタイマーを持つ
//
// 備考: 武器（または刃）の Entity に付ける。TrailRecordSystem（ロジック）が刻む
//       タイミングを決め、TrailSystem（描画）がワールド座標の2端点を記録して
//       加算合成のリボンを作る。
//       断面は age を持ち、lifeTime を超えたものから古い順に捨てる（尾が伸び縮みする）。
//       リングはヒープを使わない固定長配列で、保存するのは設定値だけ。
//       止まっているときに軌跡が溜まらないよう、先端が minDistance 以上動いたときだけ
//       新しい断面を刻む（速く振ったときだけ尾が出る）。
//       emitting は既定では手動。animControlled=true なら TrailAnimationSyncSystem が
//       祖先の Animator の Trail 区間イベントを見て毎フレーム書き換える。
//
//       時間は2つの時計で進める。断面の老化（尾が消える速さ）はこの Entity 自身の
//       TimeStateComponent（animControlled なら持ち主の時計）、一時トレイルの残り時間は
//       Animator を持つ祖先（ownerEntity）の TimeStateComponent。詳しくは TrailRecordSystem.h。
// ===========================================================================
#include "Engine/ECS/Common/CCL_Types.h"
#include "Engine/Graphics/Resource/ResourceTypes.h"
#include <DirectXMath.h>
#include <cstdint>
#include <string>

// 断面の作り方。
// Ribbon は板1枚なので、面が視線と平行になると線につぶれる。剣の軌跡は振る面を
// 見せるものなのでそれでよいが、足元の尾のようにどこから見ても太さが欲しいときは
// Cross / Tube を使う。
enum class TrailShape : uint8_t
{
    Ribbon = 0, // 根本→先端の線分を掃いた板1枚（剣向き）
    Cross  = 1, // 板を進行方向まわりに複製して十字にする（軽い。真横から見ても消えない）
    Tube   = 2, // 断面を円にして押し出した筒（足元の尾など、太さが欲しいとき）
};

// CFIELD_ENUM_U8 用の表示名（TrailShape の並びと一致させる）
static constexpr const char *TrailShapeNames[] = {
    "Ribbon (板1枚)",
    "Cross (十字)",
    "Tube (筒)",
};
static constexpr int TrailShapeCount = 3;

struct TrailComponent
{
    // ============================================================
    // 設定（保存する。座標は Entity のローカル空間）
    // ============================================================
    DirectX::XMFLOAT3 baseOffset = {0.0f, 0.0f, 0.0f};       // 刃の根本（ローカル, m）
    DirectX::XMFLOAT3 tipOffset  = {0.0f, 1.0f, 0.0f};       // 刃の先端（ローカル, m）
    DirectX::XMFLOAT4 color      = {0.6f, 0.8f, 1.0f, 1.0f}; // 加算する色（rgb）と基準のアルファ（a）

    // トレイルに貼るテクスチャの相対パス（例: "Assets/Textures/Trail/slash.dds"）。
    // 空なら白テクスチャ扱いで単色になる。設定すると色 × テクスチャで描く。
    // U は尾の長さ方向（先端 0 → 末尾 1）、V は幅方向（根本 0 → 先端 1）。
    // アルファで輪郭の形、RGB で模様、U 方向のグラデーションで寿命による色の変化を出せる。
    std::string texturePath;

    DirectX::XMFLOAT2 uvTiling = {1.0f, 1.0f}; // UV の繰り返し回数（x: 長さ方向, y: 幅方向）
    DirectX::XMFLOAT2 uvScroll = {0.0f, 0.0f}; // UV のスクロール速度（x: 長さ方向, y: 幅方向, UV/秒）。流れる模様に使う

    float lifeTime    = 0.18f; // 1つの断面が消えるまでの時間（秒）。尾の長さになる
    float minDistance = 0.02f; // 先端がこの距離（m）以上動いたら新しい断面を刻む
    bool  emitting    = true;  // 断面を刻むか。animControlled=false のときは手動で切り替える

    bool useInterpolation = true; // 断面の間を Catmull-Rom で補間するか

    // true なら、祖先の Animator の Trail 区間イベントで emitting を切り替える。
    // false なら emitting は手動（インスペクタ、または常にオン）。
    bool animControlled = false;

    float width           = 1.0f; // トレイル全体の幅の倍率。1.0 で刃の長さのまま
    float widthCurvePower = 1.5f; // 尾に向かって細くなる強さ。大きいほど急に細くなる

    uint8_t shape = static_cast<uint8_t>(TrailShape::Ribbon); // 断面の作り方（TrailShape）

    // Cross / Tube の半径（m）。0 なら根本→先端の長さの半分を使う。
    // 足元に付けるときは baseOffset と tipOffset をほぼ同じ点にして、ここで太さを決めると扱いやすい。
    float thickness = 0.0f;

    int tubeSides = 6; // Tube の円周の分割数（3〜16）。増やすほど滑らかで重い

    // Tube の輪郭の強調（0〜1）。1 で輪郭だけ光る筒、0 で均一な塊。
    // 加算合成は全面が同じ明るさだと平たく見えるので、視線に対して横を向く面ほど明るくして丸みを出す。
    float rimPower = 0.7f;

    // --- 歪み（屈折）レイヤ ---
    // 背景をずらす最大量（ピクセル）。0 なら歪みレイヤを作らない。
    // 加算レイヤと同じ形を2枚目として重ね、SceneColorGrabPass が取っておいたシーンカラーを
    // 屈折させて描く。ずらす方向は面の法線をビュー空間に落とした xy なので、
    // 面がカメラを向くほど 0 に近づき、輪郭で最も強く歪む。
    float distortion = 0.0f;

    float distortionChroma = 0.0f; // 屈折の色ずれ（0〜1）。RGB のずらし量に差をつける

    // 陽炎のようなゆらぎ（0〜1）。グローバルノイズでずらす方向を乱す。
    // 板（Ribbon）が正面を向いていると法線による歪みはほぼ 0 になるので、
    // 剣の軌跡のように面を見せる使い方で歪ませたいときはここを上げる。
    float distortionNoise = 0.0f;

    float burstDuration = 0.25f; // ActivateFor で一時的にトレイルを出す時間（秒）。ダッシュなどで使う

    // ============================================================
    // 実行時状態（保存しない）
    // ============================================================
    TextureHandle     textureHandle;                   // texturePath から引いたテクスチャ。TrailSystem が毎フレーム引き直す（ResourceManager がパスでキャッシュする）
    DirectX::XMFLOAT2 uvScrollOffset = {0.0f, 0.0f};  // uvScroll を積み上げた今のずれ。TrailSystem が毎フレーム進める

    // Animator を持つ祖先。InvalidEntityID は未解決。
    // TrailAnimationSyncSystem が親をたどって探し、ここに持つ。animControlled でなくても探す
    // （一時トレイルの時計に、その持ち主の TimeStateComponent を使うため）。
    CCL::ECS::EntityID ownerEntity = CCL::ECS::InvalidEntityID;

    // 断面の老化に使っている時間の倍率（自分の時計の localDt ÷ ロジックの dt）。
    // TrailRecordSystem がロジックの1ステップごとに書き、描画側の TrailSystem が
    // UV スクロールを同じ速さで遅くするために読む。
    float timeScale = 1.0f;

    // 1フレーム分の刃の断面（根本と先端の線分）と経過時間
    struct Segment
    {
        DirectX::XMFLOAT3 base = {};   // 記録したときの根本のワールド座標（m）
        DirectX::XMFLOAT3 tip  = {};   // 記録したときの先端のワールド座標（m）
        float             age  = 0.0f; // 記録してからの経過時間（秒）。lifeTime を超えたら捨てる
    };

    // リングの容量（尾の最大の区間数）。
    // スロー中は断面が長く残るので、等速時（剣で約 11 個）よりずっと多く要る。
    // ラッシュ中の足の尾（世界 0.05 倍・寿命 0.26 秒）は実時間で約 5 秒残り、高速移動中は毎ステップ刻む。
    // 満杯になったら一番古い断面から上書きする（尾の末端が切れる）。
    // 1区間 28 バイトなので 256 で約 7KB。Entity が 16KB のチャンクに2体ずつ入る大きさに収まる。
    static constexpr int MAX_SEGMENTS = 256;

    Segment segments[MAX_SEGMENTS] = {}; // 断面のリングバッファ
    int     head                   = 0;  // 一番古い断面の添字
    int     count                  = 0;  // 有効な断面の数

    // TrailRecordSystem（ロジック, 60Hz 固定）が毎ステップ立てる「1区間刻んでよい」印。
    // 描画側の TrailSystem は、印が立っているフレームだけ断面を刻み、必ず降ろす。
    // アニメーションの再生時刻はロジックでしか進まず、剣の振りも 60Hz でしか変わらないので、描画のレートで
    // 刻むと 90Hz/144Hz の環境で姿勢が前と同じフレームができ、ルートの補間移動の分だけ
    // ずれた断面が重なって、尾が二重の線に見える。
    bool sampleRequested = false;

    float burstTimer = 0.0f; // 一時的なトレイルの残り時間（秒）

    /**
     * @brief 一時的にトレイルを出す。時間が切れると emitting が false に戻る。
     * @param duration トレイルを出す時間（秒）
     */
    void ActivateFor(float duration)
    {
        burstTimer = duration;
        emitting   = true;
    }

    // 一時トレイルのタイマーを進め、切れたら emitting を落とす
    void UpdateBurstTimer(float dt)
    {
        if (burstTimer <= 0.0f)
        {
            return;
        }

        burstTimer -= dt;

        if (burstTimer <= 0.0f)
        {
            burstTimer = 0.0f;
            emitting   = false;
        }
    }
};
