#include "VisualCameraSystem.h"

// 実装で必要なものをここで初めてインクルードする
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Platform/Input/Input.h"
#include "Engine/Platform/Input/IInputAPI.h"
#include "Engine/Core/App/GameFlowState.h"
// ※ target の役割ベース自動接続は、ゲーム側の CameraTargetBindSystem(R00b) が担う。
#include <SimpleMath.h>
#include <algorithm>
#include <cmath>
#include <memory>

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
        if (body.lockY) {
            desiredPos = XMVectorSetY(desiredPos, body.lockedY);
        }

        // 初期化フレーム対策（まだ一度も位置を書いていないならワープ）
        // 注視点側(CameraLookAtSystem)とはフラグを分けている。1bit を共有していた頃は、
        // 先に走った側がフラグを立て、もう片方が原点から補間を始めていた。
        if (!vcam.posInitialized) {
            XMStoreFloat3(&vcam.resultPos, desiredPos);
            vcam.posInitialized = true;
            vcam.RefreshValid();
            return;
        }

        // 現在位置からの補間 (Damping)
        XMVECTOR currentPos = XMLoadFloat3(&vcam.resultPos);
        // Lerp係数: 1.0 - exp(-damping * dt) がフレームレート非依存の正しい減衰計算
        float    t      = 1.0f - std::exp(-body.damping * dt);
        XMVECTOR newPos = XMVectorLerp(currentPos, desiredPos, t);

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
        if (!vcam.lookInitialized) {
            XMStoreFloat3(&vcam.resultLookAt, desiredLook);
            vcam.lookInitialized = true;
            vcam.RefreshValid();
            return;
        }

        // 現在のLookAtからの補間
        XMVECTOR currentLook = XMLoadFloat3(&vcam.resultLookAt);
        float    t           = 1.0f - std::exp(-aim.damping * dt);
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

float CameraShakeSystem::ValueNoise(std::uint32_t seed, float t)
{
    // 整数格子の乱数値を smoothstep で補間する 1D バリューノイズ。
    // rand() と違いグローバル状態を持たないので、並列実行しても再現性がある。
    const float f  = std::floor(t);
    const auto  i0 = static_cast<std::uint32_t>(static_cast<std::int32_t>(f));
    const float fr = t - f;

    auto sample = [seed](std::uint32_t i) -> float {
        // 0..1 を -1..1 へ写す
        return (HashUInt(i + seed) / static_cast<float>(0xFFFFFFFFu)) * 2.0f - 1.0f;
    };

    const float a = sample(i0);
    const float b = sample(i0 + 1u);
    const float s = fr * fr * (3.0f - 2.0f * fr); // smoothstep
    return std::lerp(a, b, s);
}

void CameraShakeSystem::Update(float dt)
{
    ForEach([&](VirtualCamera &vcam, CameraShake &shake) {
        // シェイク終了なら何もしない
        if (shake.duration <= 0.0f) return;

        // 1. 減衰計算 (Trauma Curve)
        // 時間経過とともに 1.0 -> 0.0 になる係数
        float trauma = shake.duration / shake.maxDuration;

        // 2乗することで「最初は激しく、すぐ落ち着く」自然な揺れにする
        // 線形(traumaそのまま)だと機械的な揺れに見えるため
        float shakePower = trauma * trauma * shake.amplitude;

        // 2. 時間駆動ノイズでオフセット生成
        //    経過時間 × 周波数 を位相にするので、fps が変わっても波形は同じ。
        //    以前は毎フレーム rand() を引いていたため、144fps では 60fps の
        //    2倍以上の周波数で震え、環境ごとに手触りが変わっていた。
        //    チャンネルごとにシードをずらし、軸が同じ動きをしないようにする。
        shake.elapsed += dt;
        const float phase = shake.elapsed * shake.frequency;

        const float nx = ValueNoise(shake.seed + 0u, phase) * shakePower;
        const float ny = ValueNoise(shake.seed + 977u, phase) * shakePower;
        const float rx = ValueNoise(shake.seed + 4231u, phase) * shakePower;
        const float ry = ValueNoise(shake.seed + 8419u, phase) * shakePower;

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

        // 6. 時間経過
        shake.duration -= dt;
        if (shake.duration < 0.0f) shake.duration = 0.0f;
    });
}

// =========================================================
// 4. CameraFreeControlSystem
// =========================================================

bool CameraFreeControlSystem::IsInputBlocked() const
{
    // ポーズ中 / カーソル自由(UI操作)モード中は、フリーカメラも動かさない。
    if (m_world->HasResource<GameFlowState>()) {
        const auto &flow = m_world->GetResource<GameFlowState>();
        if (flow.paused || flow.uiInteract) return true;
    }

    // エディタで他ウィンドウ(ImGuiのテキスト入力など)を操作中なら食わない。
    // これが無いと、インスペクタに数値を打ち込んでいる最中に WASD でカメラが飛ぶ。
    if (m_world->HasResource<std::shared_ptr<IInputAPI>>()) {
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
        if (rmbHeld) {
            // 感度適用 (body.lookSpeedは度数法想定: 0.2f程度)
            body.currentYaw += deltaX * body.lookSpeed;
            body.currentPitch -= deltaY * body.lookSpeed;

            // ピッチ制限 (-89 ~ 89度)
            body.currentPitch = std::clamp(body.currentPitch, -89.9f, 89.9f);

            // ヨーは無限に積算すると float 精度が落ちて回転がカクつくため畳んでおく
            body.currentYaw = std::fmod(body.currentYaw, 360.0f);

            // 移動速度調整をここ（右クリック中）に移動する
            if (wheelDelta != 0) {
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
        if (!inputBlocked) {
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
