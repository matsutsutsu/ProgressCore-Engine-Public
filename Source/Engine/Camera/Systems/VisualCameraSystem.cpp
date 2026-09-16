#include "VisualCameraSystem.h"

// 実装で必要なものをここで初めてインクルードする
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Platform/Input/Input.h"
#include "Engine/Platform/Input/IInputAPI.h"
#include "Engine/Core/App/GameFlowState.h"
#include "Engine/Camera/CameraEvents.h"     // CameraShakeRequestEvent（揺れ要求の唯一の入口）
#include "Engine/Core/Math/Interpolation.h" // Damp() / SmootherStep()
// ※ target の役割ベース自動接続は、ゲーム側の CameraTargetBindSystem(R00b) が担う。
#include <SimpleMath.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>

// Shipping(Release)では ImGui 本体もインクルードパスも構成から外れる。
#include <imgui.h>

// システムの実行順序の定義ヘッダー
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

using namespace DirectX;
using namespace DirectX::SimpleMath;

// =========================================================
// 1. CameraFollowSystem
// =========================================================
void CameraFollowSystem::Update(float dt)
{
    ForEach([&](VirtualCamera &vcam, CameraBodyFollow &body) {
        // ターゲットのTransformを取得
        auto *targetTrans = m_world->GetComponent<TransformComponent>(body.target);
        if (!targetTrans) return;

        // position（親を無視した生のローカル座標）ではなく、補間済みワールド行列の
        // 平行移動成分を使う。親子付けされたターゲットでも正しく追え、カクつきもない。
        // TPS カメラ側と同じ基準に揃えている。
        XMFLOAT3 worldPos  = targetTrans->GetRenderWorldPosition();
        XMVECTOR targetPos = XMLoadFloat3(&worldPos);
        XMVECTOR offset    = XMLoadFloat3(&body.offset);

        // シンプルな加算（本来はターゲットの回転を考慮して回すべきだが、まずは固定オフセットで）
        XMVECTOR desiredPos = targetPos + offset;

        // Y軸固定オプション：高さだけターゲットに追従させず、指定高さに貼り付ける。
        // 階段や段差でカメラが上下に跳ねるのを止めたい時に使う。
        if (body.lockY)
        {
            desiredPos = XMVectorSetY(desiredPos, body.lockedY);
        }

        // 初期化フレーム対策（まだ一度も位置を書いていないならワープ）
        // 注視点側(CameraLookAtSystem)とはフラグを分けている。1bit を共有していた頃は、
        // 先に走った側がフラグを立て、もう片方が原点から補間を始めていた。
        if (!vcam.posInitialized)
        {
            XMStoreFloat3(&vcam.resultPos, desiredPos);
            vcam.posInitialized = true;
            vcam.RefreshValid();
            return;
        }

        // 現在位置からの補間 (Damping)
        XMVECTOR currentPos = XMLoadFloat3(&vcam.resultPos);
        float    t          = CCL::Math::Damp(body.damping, dt);
        XMVECTOR newPos     = XMVectorLerp(currentPos, desiredPos, t);

        XMStoreFloat3(&vcam.resultPos, newPos);
        vcam.RefreshValid();
    });
}

// =========================================================
// 2. CameraLookAtSystem
// =========================================================
void CameraLookAtSystem::Update(float dt)
{
    ForEach([&](VirtualCamera &vcam, CameraAimLookAt &aim) {
        auto *targetTrans = m_world->GetComponent<TransformComponent>(aim.target);
        if (!targetTrans) return;

        // Follow 側と同じく補間済みワールド座標を使う
        XMFLOAT3 worldPos    = targetTrans->GetRenderWorldPosition();
        XMVECTOR targetPos   = XMLoadFloat3(&worldPos);
        XMVECTOR offset      = XMLoadFloat3(&aim.offset);
        XMVECTOR desiredLook = targetPos + offset;

        // 初期化フレーム対策
        if (!vcam.lookInitialized)
        {
            XMStoreFloat3(&vcam.resultLookAt, desiredLook);
            vcam.lookInitialized = true;
            vcam.RefreshValid();
            return;
        }

        // 現在のLookAtからの補間
        XMVECTOR currentLook = XMLoadFloat3(&vcam.resultLookAt);
        float    t           = CCL::Math::Damp(aim.damping, dt);
        XMVECTOR newLook     = XMVectorLerp(currentLook, desiredLook, t);

        XMStoreFloat3(&vcam.resultLookAt, newLook);
        vcam.RefreshValid();
    });
}

// =========================================================
// 3. CameraShakeSystem
// =========================================================

std::uint32_t CameraShakeSystem::HashUInt(std::uint32_t x)
{
    // Wang hash 相当。乗算とシフトだけで十分に散る。
    x = (x ^ 61u) ^ (x >> 16);
    x *= 9u;
    x = x ^ (x >> 4);
    x *= 0x27d4eb2du;
    x = x ^ (x >> 15);
    return x;
}

float CameraShakeSystem::GradientNoise(std::uint32_t seed, float t)
{
    // 整数格子に「値」ではなく「傾き」を配る 1D 勾配ノイズ（Perlin）。
    // rand() と違いグローバル状態を持たないので、並列実行しても再現性がある。
    //
    // 値ノイズ（格子点に値を置いて補間）ではなく勾配ノイズにしている理由:
    //   値ノイズは山と谷が必ず格子点に来るため、周波数の格子が揺れの見た目に出る。
    //   さらに乱数値がたまたま同符号で続くと、その区間は中心へ戻らず
    //   「震え」ではなく「片側へ寄る」動きになる。勾配ノイズは格子点で必ず 0 を
    //   通るので、どの区間でも中心へ戻り、常に振動として読める。
    const float f  = std::floor(t);
    const auto  i0 = static_cast<std::uint32_t>(static_cast<std::int32_t>(f));
    const float fr = t - f;

    auto gradient = [seed](std::uint32_t i) -> float {
        // 0..1 を -1..1 へ写す
        return (HashUInt(i + seed) / static_cast<float>(0xFFFFFFFFu)) * 2.0f - 1.0f;
    };

    // 各格子点の傾きから見た「そこからの高さ」。格子点上では必ず 0 になる。
    const float v0 = gradient(i0) * fr;
    const float v1 = gradient(i0 + 1u) * (fr - 1.0f);

    // 格子をまたぐ瞬間に加速度が折れないよう SmoothStep ではなく SmootherStep を使う。
    // ここで折れると、周波数の格子が規則的なカクつきとして見えてしまう。
    const float u = CCL::Math::SmootherStep(fr);

    // 1D 勾配ノイズの振れ幅はおよそ ±0.5 なので、-1..1 の想定に合わせて広げる。
    return std::lerp(v0, v1, u) * 2.0f;
}

void CameraShakeSystem::Initialize()
{
    // 画面を揺らす要求の受け口。「誰を揺らすか」は CameraShake が付いているカメラ、
    // という規則にしてあるので、要求側はカメラを名指しする必要がない。
    ListenEvent<CameraShakeRequestEvent>([this](const CameraShakeRequestEvent &ev) {
        // ここではコンポーネントを触らず、要求量だけを受け止める。
        //   ・イベントはロジック側の任意のスレッドから同期的に飛んでくるため、
        //     直接書くと同フレームの他の要求と読み書きが競合しうる。
        //   ・「同一フレームは最大値だけ採る」を成立させるにも一度集約が要る。
        //     ドローン多数に同時命中すると要求も同数飛ぶので、足し合わせると
        //     1回の斬撃で最大の揺れに飽和してしまう。フレームをまたいだ分は
        //     AddTrauma 側で積み上がるので、連撃が続けば揺れは育つ。
        float current = m_pendingTrauma.load(std::memory_order_relaxed);
        while (ev.trauma > current &&
               !m_pendingTrauma.compare_exchange_weak(
                   current, ev.trauma, std::memory_order_relaxed))
        {
        }
    });
}

void CameraShakeSystem::Update(float dt)
{
    // 要求の反映はここだけ。CameraShake への書き込み口を1箇所に閉じておく。
    const float pending = m_pendingTrauma.exchange(0.0f, std::memory_order_relaxed);

    m_shakableCameraCount = 0;

    ForEach([&](VirtualCamera &vcam, CameraShake &shake) {
        if (pending > 0.0f) shake.AddTrauma(pending);

        // エディタ表示用。「要求が届いていない」のか「弱くて見えない」のかは
        // 画面を見ても区別できないので、数字で出せるようにしておく。
        ++m_shakableCameraCount;
        m_lastTrauma = shake.trauma;

        // シェイク終了なら何もしない
        if (shake.trauma <= 0.0f) return;

        // 1. 蓄積量から今フレームの強さを出す
        //    2乗することで「最初は激しく、すぐ落ち着く」自然な揺れにする。
        //    線形(traumaそのまま)だと機械的な揺れに見えるため。
        const float shakePower = shake.trauma * shake.trauma * shake.amplitude;

        // 蓄積の減衰はここで済ませておく。以降には「視線が縮退していたら諦める」
        // early return があり、後ろに置くと揺れが永久に止まらなくなる。
        shake.trauma -= shake.decayRate * dt;
        if (shake.trauma < 0.0f) shake.trauma = 0.0f;

        // 2. 時間駆動ノイズでオフセット生成
        //    経過時間 × 周波数 を位相にするので、fps が変わっても波形は同じ。
        //    チャンネルごとにシードをずらし、軸が同じ動きをしないようにする。
        shake.elapsed += dt;
        const float phase = shake.elapsed * shake.frequency;

        const float nx = GradientNoise(shake.seed + 0u, phase) * shakePower;
        const float ny = GradientNoise(shake.seed + 977u, phase) * shakePower;
        const float rx = GradientNoise(shake.seed + 4231u, phase) * shakePower;
        const float ry = GradientNoise(shake.seed + 8419u, phase) * shakePower;

        // 3. カメラのローカル軸を求める
        //    ワールド軸で揺らすと、カメラの向き次第で「手前に突っ込む揺れ」になる。
        //    画面の右/上方向へ揺らせば、どの向きでも同じ見え方になる。
        Vector3 eye  = vcam.resultPos;
        Vector3 look = vcam.resultLookAt;
        Vector3 up   = vcam.resultUp;

        Vector3 forward = look - eye;
        if (forward.LengthSquared() < 1e-8f) return; // 視線が縮退していたら揺らさない
        forward.Normalize();

        Vector3 right = up.Cross(forward);
        if (right.LengthSquared() < 1e-8f) return; // 真上/真下を向いていて右が取れない
        right.Normalize();

        Vector3 localUp = forward.Cross(right);
        localUp.Normalize();

        // 4. 平行移動の揺れ：視点と注視点を同じだけずらす（＝カメラごと平行に震える）
        const Vector3 posOffset = right * nx + localUp * ny;
        Vector3       newEye    = eye + posOffset;
        Vector3       newLook   = look + posOffset;

        // 5. 回転の揺れ：注視点だけを余分にずらす（＝画面が振れる）
        //    平行移動だけだと「衝撃」に見えないので、少量だけ回転成分を混ぜる。
        newLook += (right * rx + localUp * ry) * shake.rotationRatio;

        vcam.resultPos    = newEye;
        vcam.resultLookAt = newLook;
    });
}

void CameraShakeSystem::OnGui()
{
    ImGui::Text("揺らせるカメラ: %d 台", m_shakableCameraCount);
    if (m_shakableCameraCount == 0)
    {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                           "Camera Shake が付いたカメラが無い。要求は捨てられている。");
    }

    ImGui::Text("蓄積量: %.3f", m_lastTrauma);
    ImGui::ProgressBar(m_lastTrauma, ImVec2(-1.0f, 0.0f));

    // 揺れは trauma の2乗に比例する。0.3 と 0.6 では4倍違うので、
    // 発生源を動かさずに強さの当たりを付けられるようにしておく。
    ImGui::TextUnformatted("テスト発火:");
    for (float t : {0.25f, 0.5f, 1.0f})
    {
        ImGui::SameLine();
        ImGui::PushID(static_cast<int>(t * 100.0f));
        char label[16];
        std::snprintf(label, sizeof(label), "%.2f", t);
        // 発生源と同じ経路を通す。ここだけ直接 AddTrauma すると検証にならない。
        if (ImGui::SmallButton(label))
            m_world->GetEventBus().Publish(CameraShakeRequestEvent{t});
        ImGui::PopID();
    }
}

// =========================================================
// 4. CameraFreeControlSystem
// =========================================================

bool CameraFreeControlSystem::IsInputBlocked() const
{
    // ポーズ中 / カーソル自由(UI操作)モード中は、フリーカメラも動かさない。
    if (m_world->HasResource<GameFlowState>())
    {
        const auto &flow = m_world->GetResource<GameFlowState>();
        if (flow.paused || flow.uiInteract) return true;
    }

    // エディタで他ウィンドウ(ImGuiのテキスト入力など)を操作中なら食わない。
    // これが無いと、インスペクタに数値を打ち込んでいる最中に WASD でカメラが飛ぶ。
    if (m_world->HasResource<std::shared_ptr<IInputAPI>>())
    {
        auto inputAPI = m_world->GetResource<std::shared_ptr<IInputAPI>>();
        if (inputAPI && !inputAPI->IsGameInputActive()) return true;
    }
    return false;
}

void CameraFreeControlSystem::Update(float dt)
{
    // シングルトンから入力取得
    auto &input = Input::Instance();
    auto &mouse = input.GetMouse();
    auto &kb    = input.GetKeyboard();

    using namespace DirectX;

    // TPS カメラ側(cursorFreeOverride)と同じ条件で入力を遮断する。
    // 遮断中も vcam の同期だけは続けたいので、入力量をゼロにする形で扱う。
    const bool inputBlocked = IsInputBlocked();

    // マウスデルタ
    float deltaX     = inputBlocked ? 0.0f : static_cast<float>(mouse.GetPositionX() - mouse.GetOldPositionX());
    float deltaY     = inputBlocked ? 0.0f : static_cast<float>(mouse.GetPositionY() - mouse.GetOldPositionY());
    int   wheelDelta = inputBlocked ? 0 : mouse.GetWheel();

    const bool rmbHeld = !inputBlocked && mouse.IsDown(Mouse::BTN_RIGHT);

    ForEach([&](VirtualCamera &vcam, CameraBodyFree &body, TransformComponent &trans) {
        // --- 1. 回転計算 (FreeCameraSystemと同じロジック) ---
        if (rmbHeld)
        {
            // 感度適用 (body.lookSpeedは度数法想定: 0.2f程度)
            body.currentYaw += deltaX * body.lookSpeed;
            body.currentPitch -= deltaY * body.lookSpeed;

            // ピッチ制限 (-89 ~ 89度)
            body.currentPitch = std::clamp(body.currentPitch, -89.9f, 89.9f);

            // ヨーは無限に積算すると float 精度が落ちて回転がカクつくため畳んでおく
            body.currentYaw = std::fmod(body.currentYaw, 360.0f);

            // 移動速度調整をここ（右クリック中）に移動する
            if (wheelDelta != 0)
            {
                body.moveSpeed *= (wheelDelta > 0) ? 1.2f : 0.8f;
                body.moveSpeed = std::clamp(body.moveSpeed, 0.1f, 1000.0f);
            }
        }

        // 度数 -> ラジアン変換
        float yawRad   = XMConvertToRadians(body.currentYaw);
        float pitchRad = XMConvertToRadians(body.currentPitch);

        // 前方ベクトル (Front) の計算: 球面座標系
        // これにより「常に水平を保ちつつ上下を向く」FPS挙動になります
        XMVECTOR Front = XMVector3Normalize(XMVectorSet(
            cosf(pitchRad) * sinf(yawRad), sinf(pitchRad), cosf(pitchRad) * cosf(yawRad), 0.0f));

        // 右ベクトル (Right) の計算
        XMVECTOR WorldUp = XMVectorSet(0, 1, 0, 0);
        XMVECTOR Right   = XMVector3Normalize(XMVector3Cross(WorldUp, Front));

        // --- 2. 移動計算 ---
        XMVECTOR Pos = XMLoadFloat3(&trans.position);
        float    s   = body.moveSpeed * dt;

        // FreeCameraComponentと同じ操作系
        if (!inputBlocked)
        {
            if (kb.IsDown('W')) Pos += Front * s;
            if (kb.IsDown('S')) Pos -= Front * s;
            if (kb.IsDown('D')) Pos += Right * s;
            if (kb.IsDown('A')) Pos -= Right * s;
            if (kb.IsDown('E')) Pos += WorldUp * s; // 真上上昇
            if (kb.IsDown('Q')) Pos -= WorldUp * s; // 真下下降
        }

        // 結果をTransformに書き戻す
        XMStoreFloat3(&trans.position, Pos);

        // 回転もTransformに反映させておく（デバッグ表示などで正しい向きに見えるように）
        // CreateFromYawPitchRoll は座標系の取り方で Pitch の符号が反転するため、
        // Front ベクトルから LookTo 行列を作り、それを逆行列化して分解する。
        XMMATRIX lookMat = XMMatrixLookToLH(XMVectorZero(), Front, WorldUp);
        // LookToはView行列(逆行列)を作るので、Inverseしてワールド回転にする
        XMVECTOR det;
        XMMATRIX worldRot = XMMatrixInverse(&det, lookMat);
        trans.rotation    = Quaternion::CreateFromRotationMatrix(worldRot);

        // --- 3. VirtualCameraへの同期 ---
        // 現在の位置
        vcam.resultPos = trans.position;

        // 見ている先 = 現在位置 + 前方ベクトル
        // (注視点を遠くに置くことで回転を安定させる)
        vcam.resultLookAt = trans.position + (Vector3(Front) * 10.0f);

        // アップベクトル
        vcam.resultUp = Vector3(0, 1, 0);

        // 位置・注視点とも自前で確定させたので採用可能
        vcam.MarkResolved();
    });
}

// =========================================================
// 自動登録マクロ群（必ず .cpp の末尾に1回だけ書く）
// =========================================================
REGISTER_RENDER_SYSTEM(CameraFollowSystem, Priority::RenderStage::R01_Camera);
REGISTER_RENDER_SYSTEM(CameraLookAtSystem, Priority::RenderStage::R01_Camera);
REGISTER_RENDER_SYSTEM(CameraShakeSystem, Priority::RenderStage::R01_Camera);
REGISTER_RENDER_SYSTEM(CameraFreeControlSystem, Priority::RenderStage::R01_Camera);
