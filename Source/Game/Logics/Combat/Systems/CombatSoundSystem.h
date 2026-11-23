#pragma once
// ===========================================================================
// 概要: 戦闘中の出来事（攻撃の命中・プレイヤーの被弾・ジャスト回避・ダッシュ・ジャンプ）に合わせて効果音を鳴らす System
//
// 責務:
//   - DamageTakenEvent を受け、プレイヤーが受けたら「被弾」、それ以外（ボス）が受けたら「命中」を鳴らす
//   - JustEvadeSuccessEvent を受け、「ジャスト回避」と「スローの始まり」を鳴らす
//   - PlayerDashedEvent を受け、「ダッシュ」を鳴らす
//   - PlayerJumpedEvent を受け、「ジャンプ」を鳴らす（1段目は低め、2段目は高め）
//
// 備考: イベントを受けるだけにして、ダメージや移動の System が音を知らないようにする
//       素振りや着地のようにアニメのタイミングで決まる音は、アニメのシーケンス（Sound イベント）に置く
//       プレイヤーへの手応えなので、距離で小さくならないよう 2D で鳴らす。イベント名は Assets/Audio/SE の下のファイル名
//
//       ダッシュとジャンプは回数が多いので、毎回ピッチと音量を少し揺らす
//       SE_PlayerJump_01 のように「元の名前_〇〇」のファイルを置くと候補に加わり、毎回その中から選ぶ
//
//       音量とピッチは OnGui で試せる。再起動で戻るので、決まった値はこのヘッダの初期値に書く
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"

#include <cstdint>
#include <random>
#include <vector>

class IAudioAPI;

class CombatSoundSystem : public CCL::ECS::IfSystem<
    CombatSoundSystem>
{
public:
    CombatSoundSystem() : IfSystem("CombatSoundSystem") {}

    void Initialize() override;
    void Update(float dt) override; // 鳴らすのはイベントを受けたときで、毎フレームの処理はない
    void OnGui() override;

private:
    // 音ごとの鳴らし方
    struct SoundSetting
    {
        float volume = 1.0f; // 音量の倍率（1 で等倍）
        float pitch  = 0.0f; // オクターブ（0 で等倍）
    };

    // 候補の中から選んで、揺らして鳴らす音
    struct VariedSound
    {
        const char           *baseEvent;         // 元のイベント名。「これ_〇〇」も候補になる
        std::vector<uint32_t> hashes;            // 候補のイベント名のハッシュ
        bool                  collected = false;
        int                   last      = -1;    // 直前に鳴らした候補の番号
    };

    // イベント名の効果音を1回鳴らす（音声がないシーンでは何もしない）
    void Play(const char *eventName, const SoundSetting &setting);

    // 候補の中から直前と違うものを選び、ピッチと音量を少し揺らして鳴らす
    void PlayVaried(VariedSound &sound, const SoundSetting &setting);

    // 読み込み済みの音から、元の名前と「元の名前_〇〇」を候補に集める
    void CollectVariants(VariedSound &sound, const IAudioAPI &audio);

    // OnGui で1つの音の音量とピッチを出す。「鳴らす」が押されたら true
    bool DrawSetting(const char *label, SoundSetting &setting);

    // 音ごとの鳴らし方
    SoundSetting m_playerHurt    {1.3f, 0.0f};
    SoundSetting m_bossHurt      {1.6f, 0.0f};
    SoundSetting m_justEvade     {2.0f, 0.0f};
    SoundSetting m_justEvadeSlow {1.1f, 0.0f};
    SoundSetting m_dash          {3.0f, 0.0f}; // ジャンプの1段目と同じ鳴らし方
    SoundSetting m_jumpGround    {2.5f, -0.15f}; // 1段目は地面を蹴るので低く重く
    SoundSetting m_jumpAir       {2.5f, 0.1f};   // 2段目は空中なので高く軽く

    // ダッシュとジャンプの揺らぎ。毎回同じに聞こえないようにする
    float m_pitchJitter  = 0.06f; // ピッチの揺れ幅（±オクターブ。0.06 で約 ±4%）
    float m_volumeJitter = 0.1f;  // 音量の揺れ幅（±倍率）

    VariedSound  m_dashSound{"sfx:/SE_PlayerJump"}; // ダッシュもジャンプと同じ音
    VariedSound  m_jumpSound{"sfx:/SE_PlayerJump"};
    std::mt19937 m_random{std::random_device{}()};
};
