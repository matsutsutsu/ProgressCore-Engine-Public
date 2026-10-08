// 役割・設計方針は TimelineSystem.h を参照。
#include "Engine/Timeline/Systems/TimelineSystem.h"
#include "Engine/Timeline/TimelineEvents.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Platform/Input/IInputAPI.h"
#include "Engine/Platform/Core/Logger.h"
#include "Engine/Core/Components/EntityLabel.h"
#include "Engine/Core/App/GameFlowState.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Animation/Core/AnimEventMessages.h"      // Sound / Effect はアニメのイベントと同じ経路で出す
#include "Engine/Graphics/Systems/Particle/VfxSystem.h"   // VFX::Play（基準の無いエフェクト）
#include "Engine/Audio/IAudioAPI.h"                       // 2D の効果音
#include "Engine/Animation/Components/AnimatorComponent.h" // Animation トラックがクリップを握る

#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

#include <memory>
#include <vector>

using namespace CCL::ECS;

namespace
{
    // スキップ入力を受け付けるまでの猶予(秒)。
    // シーン開始直後にボタンを押しっぱなしだと事故で飛んでしまうため。
    constexpr float kSkipGraceSeconds = 0.5f;

    // 役割の結び付けを待つ上限(秒)。超えたら演出を諦めてゲームを進行させる。
    //   レベルロード完了からボスの解決まで、Debug で約1.1秒かかった実測がある（導入カメラ演出）。
    //   待ちが長引いて困るのは役割が本当に存在しない時だけなので、余裕を持たせる。
    constexpr float kRoleResolveTimeout = 5.0f;

    // 資産を読み直してクリップの対応が分からなくなったときに、握っている実体を離すブレンド時間(秒)
    constexpr float kFallbackReleaseBlend = 0.3f;

    /**
     * @brief TimelinePlayer が再生要求の対象かを返す。
     * @param ev     受けた再生要求。
     * @param id     調べる TimelinePlayer の Entity。
     * @param player 調べる TimelinePlayer。
     * @return ID → きっかけ名の順に見て一致すれば true。どちらも指定の無い要求なら常に true。
     */
    bool IsRequestTarget(const TimelinePlayRequestEvent &ev, EntityID id, const TimelinePlayerComponent &player)
    {
        if (ev.playerID != InvalidEntityID) return ev.playerID == id;
        if (!ev.trigger.empty()) return ev.trigger == player.trigger;
        return true;
    }
} // namespace

// ---------------------------------------------------------------------------
// 初期化・資産の読み込み・役割の解決
// ---------------------------------------------------------------------------

void TimelineSystem::Initialize()
{
    ListenEvent<TimelinePlayRequestEvent>([this](const TimelinePlayRequestEvent &ev) {
        bool any = false;
        ForEachWithID([&](EntityID id, TimelinePlayerComponent &player) {
            if (!IsRequestTarget(ev, id, player)) return;
            player.RequestPlay();
            any = true;
        });

        // 名前の打ち間違いに気付けるように残す。
        // ただしムービーを置いていないシーン（EditorScene など）でも来る要求なので、警告にはしない。
        if (!any && !ev.trigger.empty())
            CCL_LOG_INFO(LogCategory::Game, "[Timeline] きっかけ '%s' の TimelinePlayer が無いので再生しません",
                         ev.trigger.c_str());
    });
}

void TimelineSystem::LoadAssetIfNeeded(TimelinePlayerComponent &player) const
{
    if (player.asset || player.hasLoadError || player.assetPath.empty()) return;

    auto        asset = std::make_shared<TimelineAsset>();
    std::string err;
    if (!TimelineAssetLoader::Load(player.assetPath, *asset, err))
    {
        player.hasLoadError = true;
        CCL_LOG_ERROR(LogCategory::Game, "[Timeline] 読み込みに失敗: %s (%s)",
                      player.assetPath.c_str(), err.c_str());
        return;
    }

    // アニメの長さが決まってから全体の長さを計算し直す（長さを省略したアニメの終わりまで再生を続けるため）
    TimelineAssetLoader::ResolveAnimations(*asset);
    TimelineAssetLoader::Normalize(*asset);

    CCL_LOG_INFO(LogCategory::Game, "[Timeline] 読み込み: %s (%.2fs, カメラ %zu / SE %zu / エフェクト %zu / アニメ %zu)",
                 player.assetPath.c_str(), asset->duration, asset->cameraShots.size(), asset->sounds.size(),
                 asset->effects.size(), asset->animations.size());
    player.asset = std::move(asset);
}

EntityID TimelineSystem::ResolveRole(const TimelinePlayerComponent &player, const std::string &role) const
{
    const auto it = player.bindings.find(role);
    if (it == player.bindings.end()) return InvalidEntityID;
    if (!m_world->IsEntityValid(it->second)) return InvalidEntityID;
    if (!m_world->GetComponent<TransformComponent>(it->second)) return InvalidEntityID;
    return it->second;
}

bool TimelineSystem::AreRolesBound(const TimelinePlayerComponent &player) const
{
    for (const auto &role : player.asset->CollectRoles())
    {
        if (ResolveRole(player, role) == InvalidEntityID) return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// 点イベント（SE・エフェクト・シグナル）
// ---------------------------------------------------------------------------

void TimelineSystem::CollectClipFires(EntityID id, TimelinePlayerComponent &player,
                                      std::vector<std::function<void()>> &out) const
{
    const TimelineAsset &asset = *player.asset;

    // スキップはそのステップのうちに終端まで飛んで終わるので、立っていれば「このステップで飛んだ」
    const bool skipping = player.skipped;

    while (player.nextSound < asset.sounds.size() && asset.sounds[player.nextSound].time <= player.time)
    {
        const SoundClip &clip = asset.sounds[player.nextSound++];
        if (skipping && !clip.fireOnSkip) continue;
        QueueSound(player, clip, out);
    }

    while (player.nextEffect < asset.effects.size() && asset.effects[player.nextEffect].time <= player.time)
    {
        const EffectClip &clip = asset.effects[player.nextEffect++];
        if (skipping && !clip.fireOnSkip) continue;
        QueueEffect(player, clip, out);
    }

    // シグナルは意味を知らないので、名前と引数をそのまま渡すだけ（受け手はゲーム側）
    while (player.nextSignal < asset.signals.size() && asset.signals[player.nextSignal].time <= player.time)
    {
        const SignalClip &clip = asset.signals[player.nextSignal++];
        if (skipping && !clip.fireOnSkip) continue;
        if (clip.name.empty()) continue;

        out.push_back([this, ev = TimelineSignalEvent{id, clip.name, clip.param, clip.param2, clip.value, skipping}]() {
            m_world->GetEventBus().Publish(ev);
        });
    }
}

void TimelineSystem::QueueSound(const TimelinePlayerComponent &player, const SoundClip &clip,
                                std::vector<std::function<void()>> &out) const
{
    if (clip.event.empty()) return;

    if (clip.spatial)
    {
        // 3D はアニメの Sound イベントと同じ経路（AudioSystem）で鳴らす。発音位置はその瞬間の役割の位置
        const EntityID entity = ResolveRole(player, clip.subject);
        if (entity == InvalidEntityID)
        {
            CCL_LOG_WARN(LogCategory::Game, "[Timeline] SE の発音位置の役割 '%s' が無いので鳴らしません (%s)",
                         clip.subject.c_str(), clip.event.c_str());
            return;
        }
        out.push_back([this, msg = AnimEventSoundMessage{entity, clip.event, clip.volume, clip.pitch}]() {
            m_world->GetEventBus().Publish(msg);
        });
        return;
    }

    // 2D は距離減衰なしで直接鳴らす（AudioSystem の受け口は 3D 用しか無いため）
    out.push_back([this, hash = CCL::Utils::HashString(clip.event.c_str()), volume = clip.volume,
                   pitch = clip.pitch]() {
        if (!m_world->HasResource<std::shared_ptr<IAudioAPI>>()) return;
        auto &api = m_world->GetResource<std::shared_ptr<IAudioAPI>>();
        if (api) api->PlayOneShot(hash, volume, pitch);
    });
}

void TimelineSystem::QueueEffect(const TimelinePlayerComponent &player, const EffectClip &clip,
                                 std::vector<std::function<void()>> &out) const
{
    if (clip.effect.empty()) return;

    if (clip.subject.empty())
    {
        // 基準が無ければ offset をワールド座標として出す。
        //   この経路は止める手段を持たないので、継続放出の VFX を指定すると出続ける。バースト専用。
        out.push_back([this, path = clip.effect, pos = clip.offset]() {
            VFX::Play(*m_world, path, pos);
        });
        return;
    }

    // 基準があればアニメの Effect イベントと同じ経路（VFXSystem）で出す。
    // 向き・大きさ・追従・武器基準・継続放出の停止まで、アニメのシーケンサーと同じ扱いになる
    const EntityID entity = ResolveRole(player, clip.subject);
    if (entity == InvalidEntityID)
    {
        CCL_LOG_WARN(LogCategory::Game, "[Timeline] エフェクトの基準の役割 '%s' が無いので出しません (%s)",
                     clip.subject.c_str(), clip.effect.c_str());
        return;
    }
    out.push_back([this, msg = AnimEventEffectMessage{entity, clip.effect, clip.offset, clip.scale, clip.follow,
                                                      clip.duration, clip.attachTag}]() {
        m_world->GetEventBus().Publish(msg);
    });
}

// ---------------------------------------------------------------------------
// 再生の進行（スキップ・中止・開始待ち・終端）
// ---------------------------------------------------------------------------

bool TimelineSystem::ReadSkipInput() const
{
    if (!m_world->HasResource<std::shared_ptr<IInputAPI>>()) return false;
    auto inputAPI = m_world->GetResource<std::shared_ptr<IInputAPI>>();
    if (!inputAPI || !inputAPI->IsGameInputActive()) return false;
    return inputAPI->GetAction("Attack") || inputAPI->GetAction("Jump");
}

void TimelineSystem::Abort(TimelinePlayerComponent &player) const
{
    player.state         = TimelinePlayState::Finished;
    player.autoPlayArmed = false;
    player.previewing    = false;
}

bool TimelineSystem::Advance(EntityID id, TimelinePlayerComponent &player, float dt, bool skipRequested) const
{
    // 1. 自動再生の起動
    if (player.state == TimelinePlayState::Idle && player.autoPlayArmed && player.playOnStart)
        player.RequestPlay();

    // 2. 開始待ち：startDelay の消化と、役割の結び付けの両方を待つ
    if (player.state == TimelinePlayState::Waiting)
    {
        player.delayTimer += dt;

        if (!AreRolesBound(player))
        {
            player.resolveTimer += dt;
            if (player.resolveTimer < kRoleResolveTimeout) return false;

            // ここで諦めないと、役割が現れないまま cinematic が立ち続け、操作できなくなる。
            // 演出が失敗したときは必ずゲームを進める側に倒す。
            CCL_LOG_WARN(LogCategory::Game,
                         "[Timeline] 役割が %.1f 秒結び付かないため中止します (player=%s, asset=%s)",
                         kRoleResolveTimeout, CCL::EntityLabel(*m_world, id).c_str(),
                         player.assetPath.c_str());
            Abort(player);
            return true;
        }

        if (player.delayTimer < player.startDelay) return false;

        player.state    = TimelinePlayState::Playing;
        player.time     = 0.0f;
        player.prevTime = 0.0f;
        CCL_LOG_INFO(LogCategory::Game, "[Timeline] 再生開始 (player=%s, asset=%s, %.2fs)",
                     CCL::EntityLabel(*m_world, id).c_str(), player.assetPath.c_str(),
                     player.asset->duration);
        return false;
    }

    if (player.state != TimelinePlayState::Playing) return false;

    // 3. 再生位置を進める
    player.prevTime = player.time;
    player.time += dt;

    // 4. スキップ
    //    その場で止めず再生位置を終端へ飛ばすだけにする。すぐ下の終端判定で通常どおり終わり、
    //    カメラは CameraBrainSystem のブレンドでプレイヤーカメラへ戻る。
    if (skipRequested && player.allowSkip && player.time > kSkipGraceSeconds &&
        player.time < player.asset->duration)
    {
        player.skipped  = true;
        player.time     = player.asset->duration;
        player.prevTime = player.time; // 補間で途中のショットを1フレーム挟まないように
    }

    // 5. 終端
    if (player.time >= player.asset->duration)
    {
        player.time  = player.asset->duration;
        player.state = TimelinePlayState::Finished;
        CCL_LOG_INFO(LogCategory::Game, "[Timeline] 再生終了 (player=%s, skipped=%s)",
                     CCL::EntityLabel(*m_world, id).c_str(), player.skipped ? "yes" : "no");
        return true;
    }
    return false;
}

bool TimelineSystem::StepPlayback(EntityID id, TimelinePlayerComponent &player, float dt, bool skipRequested,
                                  std::vector<std::function<void()>> &fires,
                                  std::vector<TimelineFinishedEvent>  &finished) const
{
    // 再生待ちのものは資産を読まない（使わないかもしれないので）
    const bool wantsPlay = player.state == TimelinePlayState::Waiting ||
                           player.state == TimelinePlayState::Playing ||
                           (player.state == TimelinePlayState::Idle && player.autoPlayArmed && player.playOnStart);
    if (!wantsPlay) return false;

    LoadAssetIfNeeded(player);
    if (!player.asset)
    {
        // 読めないものは再生できない。待たせ続けると操作できなくなるので、即座に打ち切る
        Abort(player);
        finished.push_back({id, false, true});
        return false;
    }

    const bool wasWaiting   = (player.state == TimelinePlayState::Waiting);
    const bool justFinished = Advance(id, player, dt, skipRequested);

    // Waiting から直接 Finished になるのは中止だけ。中止のときは何も鳴らさない
    const bool aborted = justFinished && wasWaiting;
    if (!aborted && (player.state == TimelinePlayState::Playing || justFinished))
        CollectClipFires(id, player, fires);

    if (justFinished)
    {
        finished.push_back({id, player.skipped, aborted});
        return false;
    }

    return player.state == TimelinePlayState::Waiting || player.state == TimelinePlayState::Playing;
}

// ---------------------------------------------------------------------------
// アニメの乗っ取り（Animation トラック）
// ---------------------------------------------------------------------------

void TimelineSystem::SyncAnimationClips(TimelinePlayerComponent &player) const
{
    const std::vector<AnimationClip> *clips = player.asset ? &player.asset->animations : nullptr;
    const std::size_t                 count = clips ? clips->size() : 0;

    // 資産が読み直されて本数が変わった（インスペクタの「読み直す」など）。
    // 添字の対応が崩れたので、握っている実体を全部離してから数え直す
    if (player.animHeld.size() != count)
    {
        for (EntityID held : player.animHeld)
        {
            if (held != InvalidEntityID) ReleaseAnimation(held, kFallbackReleaseBlend);
        }
        player.animHeld.assign(count, InvalidEntityID);
    }
    if (!clips) return;

    const bool playing = (player.state == TimelinePlayState::Playing);
    auto inRange = [&](const AnimationClip &c) {
        return playing && c.resolved && player.time >= c.start && player.time < c.End();
    };

    // 1. 区間を出たものを離す。
    //    握るより先に離す。同じ相手へ続けて別のクリップを渡すとき、後から離すと
    //    いま握ったばかりのクリップまで手放してしまう
    for (std::size_t i = 0; i < count; ++i)
    {
        if (player.animHeld[i] == InvalidEntityID || inRange((*clips)[i])) continue;
        ReleaseAnimation(player.animHeld[i], (*clips)[i].blendOut);
        player.animHeld[i] = InvalidEntityID;
    }

    // 2. 区間に入ったものを握る
    for (std::size_t i = 0; i < count; ++i)
    {
        if (player.animHeld[i] != InvalidEntityID || !inRange((*clips)[i])) continue;

        const EntityID entity = ResolveRole(player, (*clips)[i].subject);
        if (entity != InvalidEntityID && StartAnimation(entity, (*clips)[i]))
            player.animHeld[i] = entity;
    }
}

bool TimelineSystem::StartAnimation(EntityID entity, const AnimationClip &clip) const
{
    auto *animator = m_world->GetComponent<AnimatorComponent>(entity);
    if (!animator) return false;

    // ステートに入るときと同じく、同じクリップでも頭から再生し直す（理由は PlayParams::restart）
    animator->Play(clip.resolved, {.blendTime = clip.blendIn, .loop = clip.loop, .speed = clip.speed, .restart = true});
    animator->isExternalOverride   = true;
    animator->externalReleaseBlend = -1.0f; // 直前に離した要求が残っていれば取り消す（続けて握り直すため）
    return true;
}

void TimelineSystem::ReleaseAnimation(EntityID entity, float blendOut) const
{
    if (!m_world->IsEntityValid(entity)) return;
    auto *animator = m_world->GetComponent<AnimatorComponent>(entity);
    if (!animator) return;

    // 戻すクリップは AnimControllerSystem が決める（今のステートのクリップ）
    animator->isExternalOverride   = false;
    animator->externalReleaseBlend = blendOut;
}

// ---------------------------------------------------------------------------
// 毎ステップの入口
// ---------------------------------------------------------------------------

void TimelineSystem::Update(float dt)
{
    const bool skipRequested = ReadSkipInput();

    // 終了通知は ForEach を抜けてから発行する。
    // イベントハンドラが同じチャンクを触りうるので、走査中に発行しない。
    std::vector<TimelineFinishedEvent> finished;
    // 点イベントの発火も同じ理由で後回しにする（VFX の生成が Entity を作るため）
    std::vector<std::function<void()>> fires;

    bool anyPlayer          = false;
    bool anyCinematicActive = false;

    ForEachWithID([&](EntityID id, TimelinePlayerComponent &player) {
        anyPlayer = true;

        // スクラブ中は再生位置をインスペクタが決める。進めない（握っているアニメもそのまま）
        if (player.previewing)
        {
            LoadAssetIfNeeded(player); // 再生前にいきなりスクラブしても絵が出るように
            player.prevTime    = player.time;
            anyCinematicActive = true;
            return;
        }

        if (StepPlayback(id, player, dt, skipRequested, fires, finished))
            anyCinematicActive = true;

        // アニメの握り/離しは、再生状態がどうであれ毎ステップ合わせる。
        // 終了・スキップ・中止・最初から再生し直し、のどれでも握ったままの実体を確実に離すため
        SyncAnimationClips(player);
    });

    // カットシーン中フラグ。PlayerInputSystem・BehaviorTreeSystem・BossActionSystem がこれを見て止まる。
    //   TimelinePlayer が1つも無いシーンでは触らない。
    if (anyPlayer && m_world->HasResource<GameFlowState>())
        m_world->GetResource<GameFlowState>().cinematic = anyCinematicActive;

    // 発火を終了通知より先にする。終わりちょうどのクリップを、終了を受けて始まる処理より前に出すため
    for (const auto &fire : fires)
        fire();

    for (const auto &e : finished)
        m_world->GetEventBus().Publish(e);
}

// 入力の受付と同じ段。プレイヤー入力より先に cinematic を確定させたいが、
// PlayerInputSystem はゲーム側にあるので名前で順序を縛らない（1ステップ遅れても実害は無い）。
REGISTER_LOGIC_SYSTEM(TimelineSystem, Priority::LogicStage::L01_Input);
