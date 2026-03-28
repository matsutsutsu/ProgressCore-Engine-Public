#include "ModelRenderer.h"
#include "Engine/Core/Common/Misc.h"
#include "Engine/Graphics/Core/GpuResourceUtils.h"
#include "Engine/Graphics/Core/Graphics.h"
#include <algorithm>

#include "Editor/Utils/MaterialEditor.h"
#include "Engine/Graphics/Shader/ShaderResources.h"

#include "Engine/Graphics/Shader/Material/BasicShader.h"
#include "Engine/Graphics/Shader/Material/LambertShader.h"
#include "Engine/Graphics/Shader/Material/OutlineShader.h"
#include "Engine/Graphics/Shader/Material/PBRShader.h"
#include "Engine/Graphics/Shader/Material/PhongShader.h"
#include "Engine/Graphics/Shader/Material/ShadowMapShader.h"
#include "Engine/Graphics/Shader/Material/ToonShader.h"

ModelRenderer::ModelRenderer(ID3D11Device *device)
{
    // シェーダー生成
    shaders[static_cast<int>(ShaderId::Basic)]     = std::make_unique<BasicShader>(device);
    shaders[static_cast<int>(ShaderId::Lambert)]   = std::make_unique<LambertShader>(device);
    shaders[static_cast<int>(ShaderId::Phong)]     = std::make_unique<PhongShader>(device);
    shaders[static_cast<int>(ShaderId::Toon)]      = std::make_unique<ToonShader>(device);
    shaders[static_cast<int>(ShaderId::Outline)]   = std::make_unique<OutlineShader>(device);
    shaders[static_cast<int>(ShaderId::ShadowMap)] = std::make_unique<ShadowMapShader>(device);
    shaders[static_cast<int>(ShaderId::PBR)]       = std::make_unique<PBRShader>(device);

    // GPUインスタンシングのバッファ作成
    _instanceBuffer.Create(device, 8192); // 8192個まで対応

    // グローバル定数バッファの初期化
    InitializeGlobalConstantBuffers(device);

    OutputDebugStringA("ModelRenderer: Initialized with shader reflection\n");
}

void ModelRenderer::InitializeGlobalConstantBuffers(ID3D11Device *device)
{
    // 全シェーダーから共通の定数バッファ情報を収集
    std::map<std::string, ShaderReflect::ConstantBufferInfo> uniqueBuffers;

    for (int i = 0; i < static_cast<int>(ShaderId::EnumCount); ++i) {
        if (!shaders[i]) continue;

        // 各シェーダーのリフレクション情報から CbScene と CbSkeleton を探す
        const auto &reflection = shaders[i]->GetReflection();

        for (const auto &cbInfo : reflection.GetConstantBuffers()) {
            // CbScene, CbSkeleton などの共通バッファを登録
            if (cbInfo.name == "CbScene" || cbInfo.name == "CbSkeleton") {
                // 既に登録されていない、またはより大きいサイズの場合に更新
                if (uniqueBuffers.find(cbInfo.name) == uniqueBuffers.end() ||
                    uniqueBuffers[cbInfo.name].size < cbInfo.size) {
                    uniqueBuffers[cbInfo.name] = cbInfo;
                }
            }
        }
    }

    // グローバル定数バッファを作成
    for (const auto &[name, info] : uniqueBuffers) {
        ShaderReflect::AutoConstantBuffer buffer;
        if (buffer.Create(device, info)) {
            globalConstantBuffers[name] = std::move(buffer);

            char buf[256];
            sprintf_s(buf,
                "Created global constant buffer: %s (slot: b%d, size: %d)\n",
                name.c_str(),
                info.bindPoint,
                info.size);
            OutputDebugStringA(buf);
        }
    }

    // キャッシュを取得しておく（存在チェック）
    if (globalConstantBuffers.count("CbSkeleton")) {
        _cbSkeletonBuffer = globalConstantBuffers["CbSkeleton"].GetBuffer();
    }

    // デバッグ出力
    if (globalConstantBuffers.empty()) {
        OutputDebugStringA("Warning: No global constant buffers created!\n");
    }
}

void ModelRenderer::Draw(ShaderId      shaderId,
    const Model                       *model,
    const DirectX::XMFLOAT4X4         &worldMatrix,
    const std::vector<MaterialHandle> &overrideMaterials,
    const DirectX::XMFLOAT4           &customParams)
{
    DrawInfo &drawInfo   = drawInfos.emplace_back();
    drawInfo.shaderId    = shaderId;
    drawInfo.model       = model;
    drawInfo.worldMatrix = worldMatrix;
    drawInfo.customParams = customParams; 

    // vectorの動的アロケーションをここで断ち切り、固定配列にコピーする
    drawInfo.overrideCount =
        static_cast<uint8_t>((std::min)(overrideMaterials.size(), static_cast<size_t>(4)));
    for (uint8_t i = 0; i < drawInfo.overrideCount; ++i) {
        drawInfo.overrideMaterials[i] = overrideMaterials[i];
    }
}

void ModelRenderer::Render(const RenderContext &rc)
{
    int                  drawCallCount = 0;
    ID3D11DeviceContext *dc            = rc.deviceContext;

    //// ライト・シャドウマップの一括バインド
    //if (rc.lightManager) rc.lightManager->Bind(dc, SLOT_CB_LIGHT);
    //if (rc.shadowMap)
    //    rc.shadowMap->Bind(
    //        dc, SLOT_SRV_SHADOW, SLOT_SMP_SHADOW, SLOT_CB_SHADOW);

    // === 2. サンプラステート設定 ===
    ID3D11SamplerState *samplerStates[] = {
        rc.renderState->GetSamplerState(SamplerState::LinearWrap)};
    dc->PSSetSamplers(SLOT_SMP_LINEAR, _countof(samplerStates), samplerStates);

    // === 3. レンダーステート設定 ===
    dc->OMSetDepthStencilState(rc.renderState->GetDepthStencilState(DepthState::TestAndWrite), 0);
    dc->RSSetState(rc.renderState->GetRasterizerState(RasterizerState::SolidCullBack));

    // === 4. 視錐台カリングの準備 ===
    DirectX::XMMATRIX projection = DirectX::XMLoadFloat4x4(&rc.camera->GetProjection());
    DirectX::BoundingFrustum::CreateFromMatrix(m_frustum, projection);

    DirectX::XMMATRIX view    = DirectX::XMLoadFloat4x4(&rc.camera->GetView());
    DirectX::XMMATRIX invView = DirectX::XMMatrixInverse(nullptr, view);
    m_frustum.Transform(m_frustum, invView);

    // カメラ情報（透明度ソート用）
    DirectX::XMVECTOR CameraPosition = DirectX::XMLoadFloat3(&rc.camera->GetEye());
    DirectX::XMVECTOR CameraFront    = DirectX::XMLoadFloat3(&rc.camera->GetFront());

    // === 6. 不透明オブジェクトの描画 & 半透明の抽出 ===
    dc->OMSetBlendState(rc.renderState->GetBlendState(BlendState::Opaque), nullptr, 0xFFFFFFFF);

    // ソート（シェーダーID順 -> モデル順 -> マテリアルComponentを上書きしているかどうか）
    // これにより同じモデルが連続して並ぶようになる
    std::sort(drawInfos.begin(), drawInfos.end(), [](const DrawInfo &a, const DrawInfo &b) {
        if (a.shaderId != b.shaderId) return a.shaderId < b.shaderId;
        if (a.model != b.model) return a.model < b.model;

        // 1. まず上書きしている「数」で比較
        if (a.overrideCount != b.overrideCount) return a.overrideCount < b.overrideCount;

        // 2. 数が同じなら中身を比較（固定長なのでキャッシュに優しく高速）
        for (uint8_t i = 0; i < a.overrideCount; ++i) {
            // MaterialHandleのIDなどを比較
            // (==演算子がない場合は、内部のハンドルIDで比較してください)
            if (!(a.overrideMaterials[i] == b.overrideMaterials[i])) {
                // ここではメモリアドレスの比較ではなく、純粋な値の比較が行われる
                return a.overrideMaterials[i].index < b.overrideMaterials[i].index;
            }
        }
        return false;
    });

    // DrawInfo配列を走査
    size_t i = 0;
    while (i < drawInfos.size()) {
        const auto &current = drawInfos[i];

        // シェーダーの準備
        Shader *shader = shaders[static_cast<int>(current.shaderId)].get();
        if (!shader) {
            i++;
            continue;
        }

        shader->Begin(rc);

        // バッチ処理（同じモデルが連続している範囲を探す）
        // 開始位置: i
        size_t batchStart = i;
        size_t batchCount = 0;

        // 次の要素が自分と同じモデル・シェーダーである限りカウントアップ
        while (i < drawInfos.size()) {
            const auto &next = drawInfos[i];
            if (next.shaderId != current.shaderId || next.model != current.model ||
                next.overrideCount != current.overrideCount) // リスト全体を比較
            {
                break; // 違うモデルになったらバッチ終了
            }

            // マテリアルの完全一致チェック
            bool sameMaterials = true;
            for (uint8_t j = 0; j < current.overrideCount; ++j) {
                if (!(next.overrideMaterials[j] == current.overrideMaterials[j])) {
                    sameMaterials = false;
                    break;
                }
            }
            if (!sameMaterials) break; // 中身が一つでも違えばバッチ終了

            batchCount++;
            i++;
        }
        // ループ終了時、i は「次のバッチの先頭」を指している

        // --- インスタンスデータの準備 (不透明用) ---
        // このバッチで使う「行列リスト」を作成してGPUに送る
        static std::vector<InstanceData>
            instanceDataCache; // staticにして再利用（メモリ確保回数を減らす）
        instanceDataCache.clear();
        instanceDataCache.reserve(batchCount);

        for (size_t k = 0; k < batchCount; ++k) {
            const auto  &info = drawInfos[batchStart + k];
            InstanceData data;
            // HLSL用に転置して格納
            // DirectX::XMStoreFloat4x4(&data.world, DirectX::XMLoadFloat4x4(&info.worldMatrix));
            data.world = info.worldMatrix;

            // 将来ディゾルブ値などを入れるための汎用パラメータ
            data.customParams = info.customParams;

            instanceDataCache.push_back(data);
        }

        // GPUバッファへ転送
        _instanceBuffer.Write(dc, instanceDataCache);
        // VSの t11 スロットにバインド
        _instanceBuffer.Bind(dc, SLOT_SRV_INSTANCE);

        // === モデル内の全メッシュを描画 ===
        // 同じモデルなので、メッシュ構造は共通。
        // メッシュごとに「インスタンス数」を指定して描画する。
        const auto &meshes = current.model->GetMeshes();
        for (size_t meshIdx = 0; meshIdx < meshes.size(); ++meshIdx) {
            const Model::Mesh &mesh = meshes[meshIdx];

            // --- マテリアル差し替えトリック ---
            Model::Mesh     &mutableMesh    = const_cast<Model::Mesh &>(mesh);
            Model::Material *originalMatPtr = mutableMesh.material;

            // オーバーライド判定
            // 「リストがあり」かつ「今のメッシュ番号に対応するデータがあり」かつ「nullptrじゃない」場合
            MaterialData *overrideData = nullptr;

            if (meshIdx < current.overrideCount) {
                // ハンドルを使って実体を取りに行く
                MaterialHandle handle = current.overrideMaterials[meshIdx];
                if (handle.IsValid()) {
                    overrideData = ResourceManager::Instance().GetMaterial(handle);
                }
            }

            Model::Material tempMatWrapper;
            bool            applyOverride = (overrideData != nullptr);

            if (applyOverride) {
                // overrideData は ResourceManager が管理しているポインタなので、
                // 勝手に delete されないように「空のデリータ」付き shared_ptr に包んで渡す
                tempMatWrapper.data =
                    std::shared_ptr<MaterialData>(overrideData, [](MaterialData *) {});
                tempMatWrapper.name  = "InstanceOverride";
                mutableMesh.material = &tempMatWrapper;
            }

            // マテリアル情報の取得 (差し替えた後のものを見る)
            auto &matData = *mutableMesh.material->data;

            // 半透明判定
            float alpha = 1.0f;
            if (matData.colors.count("materialColor")) alpha = matData.colors.at("materialColor").w;
            bool isTransparent =
                (matData.alphaMode == AlphaMode::Blend || (alpha > 0.01f && alpha < 0.99f));

            if (isTransparent) // 半透明の場合
            {
                // 半透明の場合：インスタンシングできないので、個別にリストに追加して後回し
                // バッチ内の全インスタンスについてループ
                for (size_t k = 0; k < batchCount; ++k) {
                    const auto &info = drawInfos[batchStart + k];

                    TransparencyDrawInfo &tInfo = transparencyDrawInfos.emplace_back();
                    tInfo.shaderId              = current.shaderId;
                    tInfo.mesh                  = &mesh;
                    tInfo.worldMatrix           = info.worldMatrix; // 行列を保存
                    // 対応するマテリアルを渡す
                    if (meshIdx < info.overrideCount) {
                        tInfo.overrideMaterial = info.overrideMaterials[meshIdx];
                    }
                    else {
                        tInfo.overrideMaterial = MaterialHandle{}; // 正解：空のハンドルを入れる
                    }

                    // ディゾルブなどの汎用パラメータも半透明キューへ引き継ぐ！
                    tInfo.customParams = info.customParams;

                    // 距離計算（ソート用）
                    DirectX::XMVECTOR WorldPos = DirectX::XMLoadFloat4x4(&info.worldMatrix).r[3];
                    // ※mesh.node->transform を加味する場合はここで計算が必要ですが、
                    //   簡易的にはモデルの原点(info.worldMatrix)との距離で十分です
                    tInfo.distance = DirectX::XMVectorGetX(DirectX::XMVector3Length(
                        DirectX::XMVectorSubtract(WorldPos, CameraPosition)));
                }
            }
            else // 不透明の場合
            {

                // ボーンがある場合はインスタンシングできない（今回はスキップして単体描画にフォールバック）
                // ※本格対応するにはボーン行列をテクスチャに焼くなどの高度な実装が必要
                bool isSkinned = !mesh.bones.empty();

                if (isSkinned) {
                    // --- スキンメッシュ：単体描画ループ ---
                    // バッチ内の全インスタンスについてループ
                    for (size_t k = 0; k < batchCount; ++k) {
                        // スケルトン更新（この関数内で cbSkeleton を更新している）
                        UpdateSkeletonConstants(dc, mesh);

                        // マテリアル更新
                        shader->Update(rc, mesh);

                        // 描画
                        UINT stride = sizeof(ModelResource::Vertex);
                        UINT offset = 0;
                        dc->IASetVertexBuffers(
                            0, 1, mesh.data->vertexBuffer.GetAddressOf(), &stride, &offset);
                        dc->IASetIndexBuffer(mesh.data->indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
                        dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                        dc->DrawIndexed(static_cast<UINT>(mesh.data->indices.size()), 0, 0);
                        drawCallCount++;
                    }
                }
                else {
                    // --- 静的メッシュ：インスタンシング描画 ---

                    // 1. マテリアル更新（バッチ内で共通）
                    shader->Update(rc, mesh);

                    // 2. 頂点バッファセット
                    UINT stride = sizeof(ModelResource::Vertex);
                    UINT offset = 0;
                    dc->IASetVertexBuffers(
                        0, 1, mesh.data->vertexBuffer.GetAddressOf(), &stride, &offset);
                    dc->IASetIndexBuffer(mesh.data->indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
                    dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

                    // 3. インスタンシング描画発行！
                    // 第2引数に「インスタンス数（batchCount）」を渡す
                    dc->DrawIndexedInstanced(static_cast<UINT>(mesh.data->indices.size()),
                        static_cast<UINT>(batchCount), // ここがポイント！
                        0,
                        0,
                        0);

                    drawCallCount++;
                }
            }

            // 元に戻す（重要）
            if (applyOverride) {
                mutableMesh.material = originalMatPtr;
            }
        }

        // バインド解除
        _instanceBuffer.Unbind(dc, SLOT_SRV_INSTANCE);
        shader->End(rc);
    }

    // === 7. 半透明オブジェクトの描画 (変更なし) ===
    // 半透明はソート順が命なので、インスタンシング（モデル順ソート）とは相性が悪い。
    // そのまま単体描画を行うのが一般的
    dc->OMSetBlendState(
        rc.renderState->GetBlendState(BlendState::Transparency), nullptr, 0xFFFFFFFF);

    std::sort(transparencyDrawInfos.begin(),
        transparencyDrawInfos.end(),
        [](const TransparencyDrawInfo &lhs, const TransparencyDrawInfo &rhs) {
            return lhs.distance > rhs.distance;
        });

    for (const TransparencyDrawInfo &info : transparencyDrawInfos) {
        Shader *shader = shaders[static_cast<int>(info.shaderId)].get();
        if (!shader) continue;

        shader->Begin(rc);

        // マテリアル差し替え
        const Model::Mesh &mesh           = *info.mesh;
        Model::Mesh       &mutableMesh    = const_cast<Model::Mesh &>(mesh);
        Model::Material   *originalMatPtr = mutableMesh.material;
        Model::Material    tempMatWrapper;
        // 1. ハンドルが有効かどうかチェック
        bool applyOverride = info.overrideMaterial.IsValid();

        if (applyOverride) {
            // 2. ハンドルから実体（生ポインタ）を取得
            MaterialData *ptr = ResourceManager::Instance().GetMaterial(info.overrideMaterial);

            if (ptr) {
                // 3. 空のデリータ付き shared_ptr に包んで渡す
                tempMatWrapper.data  = std::shared_ptr<MaterialData>(ptr, [](MaterialData *) {});
                tempMatWrapper.name  = "TranspOverride";
                mutableMesh.material = &tempMatWrapper;
            }
            else {
                applyOverride = false; // ポインタが取れなければ無効化
            }
        }

        // 1. データ作成
        InstanceData singleData;
        singleData.world = info.worldMatrix;

        // 汎用パラメータ初期化
        singleData.customParams = info.customParams;

        // 2. バッファの0番目に書き込む（vectorのサイズ1で渡せばOK）
        // 毎回 Map/Unmap が発生しますが、半透明の数は少ないので許容範囲です
        std::vector<InstanceData> temp = {singleData};
        _instanceBuffer.Write(dc, temp);

        // 3. バインド (t11にセット)
        _instanceBuffer.Bind(dc, SLOT_SRV_INSTANCE);

        // 4. マテリアル更新
        // （定数バッファの更新などは drawMesh と同じ）
        UpdateSkeletonConstants(dc, mesh); // スキンメッシュ対応
        shader->Update(rc, mesh);

        // 5. 描画
        UINT stride = sizeof(ModelResource::Vertex);
        UINT offset = 0;
        dc->IASetVertexBuffers(0, 1, mesh.data->vertexBuffer.GetAddressOf(), &stride, &offset);
        dc->IASetIndexBuffer(mesh.data->indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
        dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        // DrawIndexed を呼ぶと instanceID=0 になるので、
        // さっき書き込んだ 0番目のデータ が使われる！
        dc->DrawIndexed(static_cast<UINT>(mesh.data->indices.size()), 0, 0);

        // 復元
        if (applyOverride) mutableMesh.material = originalMatPtr;

        // 6. 後始末
        _instanceBuffer.Unbind(dc, SLOT_SRV_INSTANCE);
        shader->End(rc);
    }

    // === 8. リソース解除 ===
    UnbindAllResources(dc);

    // デバッグ出力（必要に応じて）
    // char buf[128];
    // sprintf_s(buf, "Total Draw Calls: %d\n", drawCallCount);
    // OutputDebugStringA(buf);
}

void ModelRenderer::BeginFrame()
{
    // 急激にオブジェクト予約が増えたときに
    // スパイク（メモリ再確保して今ある要素を全コピーするときの負荷）
    // が起こるとFPSが一瞬下がってしまうので
    // 安定させるために要素を消した後に前の予約の数より少し多い数のメモリを確保して
    // あらかじめ用意しておく

    // 不透明オブジェクトの予約
    size_t lastOpaqueSize = drawInfos.size();
    drawInfos.clear();
    if (lastOpaqueSize > drawInfos.capacity()) {
        drawInfos.reserve(static_cast<size_t>(lastOpaqueSize * 1.1f));
    }

    //  半透明オブジェクトの予約
    size_t lastTransparentSize = transparencyDrawInfos.size();
    transparencyDrawInfos.clear();
    // 半透明は急激に増えることが多いため、少し多め（1.2倍など）にバッファを持たせても良い
    if (lastTransparentSize > transparencyDrawInfos.capacity()) {
        transparencyDrawInfos.reserve(static_cast<size_t>(lastTransparentSize * 1.2f));
    }

    //  影オブジェクトの予約
    size_t lastShadowSize = shadowDrawInfos.size();
    shadowDrawInfos.clear();
    // 半透明は急激に増えることが多いため、少し多め（1.2倍など）にバッファを持たせても良い
    if (lastShadowSize > shadowDrawInfos.capacity()) {
        shadowDrawInfos.reserve(static_cast<size_t>(lastShadowSize * 1.2f));
    }


    // ==========================================================
    // 全シェーダーのホットリロード監視
    // ==========================================================
    auto *device = Graphics::Instance().GetDevice();
    for (auto &shader : shaders) {
        if (shader) {
            shader->CheckAndReload(device);
        }
    }
}

// === 新しいメソッド：完全なリソース解除 ===
void ModelRenderer::UnbindAllResources(ID3D11DeviceContext *dc)
{
    ID3D11Buffer             *nullCB      = nullptr;
    ID3D11SamplerState       *nullSampler = nullptr;
    ID3D11ShaderResourceView *nullSRV     = nullptr;

    // 1. グローバル定数バッファの解除
    //for (auto &[name, buffer] : globalConstantBuffers) {
    //    UINT slot = buffer.GetBindPoint();
    //    dc->VSSetConstantBuffers(slot, 1, &nullCB);
    //    dc->PSSetConstantBuffers(slot, 1, &nullCB);
    //}

    // 1. グローバル定数バッファの解除 (マクロで直接指定)
    dc->VSSetConstantBuffers(SLOT_CB_SCENE, 1, &nullCB);
    dc->PSSetConstantBuffers(SLOT_CB_SCENE, 1, &nullCB);

    dc->VSSetConstantBuffers(SLOT_CB_SKELETON, 1, &nullCB);
    dc->PSSetConstantBuffers(SLOT_CB_SKELETON, 1, &nullCB);

    // 2. サンプラーステートの解除
    dc->PSSetSamplers(0, 1, &nullSampler);

    // ライト・シャドウ関連の解除
    // CbLight (b9)
    dc->VSSetConstantBuffers(SLOT_CB_LIGHT, 1, &nullCB);
    dc->PSSetConstantBuffers(SLOT_CB_LIGHT, 1, &nullCB);

    // CbShadow (b10)
    dc->VSSetConstantBuffers(SLOT_CB_SHADOW, 1, &nullCB);
    dc->PSSetConstantBuffers(SLOT_CB_SHADOW, 1, &nullCB);

    // SrvShadow (t10)
    dc->PSSetShaderResources(SLOT_SRV_SHADOW, 1, &nullSRV);

    // SmpShadow (s10)
    dc->PSSetSamplers(SLOT_SMP_SHADOW, 1, &nullSampler);

    // ※ シェーダー固有のリソースは各シェーダーの End() で解除済み
}


void ModelRenderer::UpdateSkeletonConstants(ID3D11DeviceContext *dc, const Model::Mesh &mesh)
{
    // 文字列検索を削除！ポインタがあれば即実行
    if (!_cbSkeletonBuffer) return;

    // ボーンがある場合
    if (!mesh.bones.empty()) {
        size_t count = mesh.bones.size();
        if (count > MAX_BONES) count = MAX_BONES; // 安全対策

        for (size_t i = 0; i < count; ++i) {
            const Model::Bone &bone = mesh.bones[i];

            // 計算：Offset(初期姿勢の逆行列) * World(現在の姿勢)
            // これで「モデル空間の頂点」を直接「ワールド空間」へ持っていく行列になります
            DirectX::XMMATRIX matOffset = DirectX::XMLoadFloat4x4(&bone.data->offsetTransform);
            DirectX::XMMATRIX matWorld  = DirectX::XMLoadFloat4x4(&bone.node->worldTransform);
            DirectX::XMMATRIX matFinal  = matOffset * matWorld;

            DirectX::XMStoreFloat4x4(&m_cbSkeletonCache.boneTransforms[i], matFinal);
        }
    }
    else {
        // ボーンがない場合はメッシュ自体のワールド行列を0番に入れる（または何もしない）
        // ※シェーダー側で「ボーンなし」の分岐があるなら不要ですが、念のため
        m_cbSkeletonCache.boneTransforms[0] = mesh.node->worldTransform;
    }

    // GPU更新
    dc->UpdateSubresource(_cbSkeletonBuffer, 0, nullptr, &m_cbSkeletonCache, 0, 0);

    // 更新したバッファを b12 (CbSkeleton) に紐づける
    ID3D11Buffer *buffer = _cbSkeletonBuffer;
    dc->VSSetConstantBuffers(SLOT_CB_SKELETON, 1, &buffer);
}

void ModelRenderer::UpdateAllGlobalConstantBuffers(ID3D11DeviceContext *dc)
{
    for (auto &[name, buffer] : globalConstantBuffers) {
        // CbSkeleton は UpdateSkeletonConstants で個別更新するのでスキップ
        if (name == "CbSkeleton") continue;

        buffer.UpdateBuffer(dc);
    }
}

void ModelRenderer::BindAllGlobalConstantBuffers(ID3D11DeviceContext *dc)
{
    // リフレクションのスロット番号は一切信用せず、マクロの番号に直接バインドする
    if (globalConstantBuffers.count("CbScene")) {
        ID3D11Buffer* buf = globalConstantBuffers["CbScene"].GetBuffer();
        dc->VSSetConstantBuffers(SLOT_CB_SCENE, 1, &buf);
        dc->PSSetConstantBuffers(SLOT_CB_SCENE, 1, &buf);
    }

    if (globalConstantBuffers.count("CbSkeleton")) {
        ID3D11Buffer* buf = globalConstantBuffers["CbSkeleton"].GetBuffer();
        dc->VSSetConstantBuffers(SLOT_CB_SKELETON, 1, &buf);
        // スケルトンはVSのみで十分ですが、念のためPSにも入れておくなら追加
    }

    //for (auto &[name, buffer] : globalConstantBuffers) {
    //    buffer.BindVS(dc);
    //    buffer.BindPS(dc);
    //}
}

void ModelRenderer::DrawAllShaderGUI()
{
    ImGui::Begin("Material Debugger");

    int modelId = 0;
    // 登録されている全ての描画情報を走査
    for (const auto &drawInfo : drawInfos) {
        ImGui::PushID(modelId++);

        // モデル名でツリー作成
        if (ImGui::CollapsingHeader(drawInfo.model->GetPath().c_str())) {
            // そのモデルが持つ全メッシュのマテリアルを表示
            int meshId = 0;
            for (const auto &mesh : drawInfo.model->GetMeshes()) {
                if (mesh.material && mesh.material->data) {
                    ImGui::PushID(meshId++);

                    // マテリアル名を表示
                    if (ImGui::TreeNode(mesh.material->name.c_str())) {
                        // ここで魔法の関数を呼ぶだけ！
                        MaterialEditor::Draw(mesh.material->data.get());
                        ImGui::TreePop();
                    }

                    ImGui::PopID();
                }
            }
        }
        ImGui::PopID();
    }

    ImGui::End();
}

void ModelRenderer::PrintGlobalReflectionInfo() const
{
    OutputDebugStringA("\n=== ModelRenderer Global Constant Buffers ===\n");
    for (const auto &[name, buffer] : globalConstantBuffers) {
        char buf[256];
        sprintf_s(buf, "  [%s] Slot: b%d\n", name.c_str(), buffer.GetBindPoint());
        OutputDebugStringA(buf);

        // 変数情報も出力
        const auto &info = buffer.GetInfo();
        for (const auto &var : info.variables) {
            sprintf_s(
                buf, "    - %s (Offset: %d, Size: %d)\n", var.name.c_str(), var.offset, var.size);
            OutputDebugStringA(buf);
        }
    }
    OutputDebugStringA("=============================================\n\n");
}

void ModelRenderer::PrintAllShaderReflectionInfo() const
{
    OutputDebugStringA("\n=== All Shader Reflection Info ===\n");

    const char *shaderNames[] = {"Basic", "Lambert", "Phong", "Toon", "ShadowMap", "Outline"};

    for (int i = 0; i < static_cast<int>(ShaderId::EnumCount); ++i) {
        if (!shaders[i]) continue;

        char buf[256];
        sprintf_s(buf, "\n--- %s Shader ---\n", shaderNames[i]);
        OutputDebugStringA(buf);

        const auto &reflection = shaders[i]->GetReflection();
        reflection.PrintInfo();
    }

    OutputDebugStringA("\n=== Global Constant Buffers ===\n");
    PrintGlobalReflectionInfo();

    OutputDebugStringA("=====================================\n\n");
}

void ModelRenderer::DrawShadow(
    const Model *model, const Model::Mesh *mesh, const DirectX::XMFLOAT4X4 &worldMatrix)
{
    shadowDrawInfos.push_back({model, mesh, worldMatrix});
}

void ModelRenderer::RenderShadowPass(const RenderContext &rc)
{
    // 何もなければ帰る
    if (shadowDrawInfos.empty()) return;

    ID3D11DeviceContext *dc        = rc.deviceContext;
    auto                &shadowMap = rc.shadowMap;

    // 1. シャドウマップへの書き込みを開始
    shadowMap->Activate(dc);

    // =========================================================================
    // 前フレームのUI描画（NoWrite）などの状態を引き継がないよう、
    // ここでシャドウマップ用の「正しい描画ステート」に強制リセットする！
    // =========================================================================
    // 深度テストと書き込みを有効化 (影の深度を記録するため必須)
    dc->OMSetDepthStencilState(rc.renderState->GetDepthStencilState(DepthState::TestAndWrite), 0);
    // 半透明ブレンドを無効化 (影に半透明は不要)
    dc->OMSetBlendState(rc.renderState->GetBlendState(BlendState::Opaque), nullptr, 0xFFFFFFFF);
    // 背面カリングを有効化
    dc->RSSetState(rc.renderState->GetRasterizerState(RasterizerState::SolidCullBack));

    // 定数バッファの更新とバインド
    {
        // コンポーネントから受け取った値をShadowMapに適用
        shadowMap->params.shadowBias  = _shadowBias;
        shadowMap->params.shadowColor = _shadowColor;
        // orthoSizeはViewProj行列計算に使ったのでここでは不要（行列に含まれている）

        // (1) CbShadowのデータを生成 (受け取っておいた _shadowLightVP を使う)
        auto cbData = shadowMap->CreateCbData(_shadowLightVP);

        // (2) GPU上の定数バッファを更新
        shadowMap->UpdateConstantBuffer(dc, cbData);

        // (3) 頂点シェーダー(VS)にバインド (Slot 10)
        //     ※影生成パスではテクスチャ(SRV)やサンプラーは不要なので -1 を指定
        shadowMap->Bind(dc, -1, -1, SLOT_CB_SHADOW);
    }

    // 2. シャドウ用シェーダーを有効化
    // (ShaderId::ShadowMap に ShadowMapShader が登録されている前提)
    auto &shader = shaders[static_cast<int>(ShaderId::ShadowMap)];
    shader->Begin(rc);

    // =========================================================================
    // パーティクル(点)やデバッグ(線)のステートを引き継がないよう、
    // ここで明示的に「三角形(ポリゴン)」にリセットする！
    // =========================================================================
    dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // 3. ソート：メッシュ（マテリアル）単位で並べ替えてバッチ効率を上げる
    std::sort(shadowDrawInfos.begin(),
        shadowDrawInfos.end(),
        [](const ShadowDrawInfo &a, const ShadowDrawInfo &b) {
            // メッシュのアドレスで比較（同じメッシュが連続するように）
            return a.mesh < b.mesh;
        });

    // インスタンシング用の一時バッファ
    // （静的メッシュをここに溜め込んで一気に描く）
    struct InstanceBatch {
        const Model::Mesh        *mesh = nullptr;
        std::vector<InstanceData> data = {};
    };
    std::vector<InstanceBatch> batches;
    // メモリ確保の効率化（毎回再確保しないように）
    batches.reserve(16);

    // --- ループ処理：仕分け ---
    for (const auto &info : shadowDrawInfos) {
        // 安全対策
        if (!info.mesh || !info.mesh->data) continue;

        // ボーンがあるかチェック（スキンメッシュ判定）
        bool isSkinned = !info.mesh->bones.empty();

        if (isSkinned) {
            // === A. スキンメッシュ（キャラクター等）：個別描画 ===
            // ※現状のShadowMapShaderはスキン非対応（Tポーズになる）可能性がありますが、
            //   構造としてはここで「スキン行列を更新してDraw」を行います。

            // 将来的にはここで info.model->UpdateBoneConstants(dc) などを呼ぶ
            UpdateSkeletonConstants(dc, *info.mesh);

            // 頂点バッファセット
            UINT stride = sizeof(ModelResource::Vertex);
            UINT offset = 0;
            dc->IASetVertexBuffers(
                0, 1, info.mesh->data->vertexBuffer.GetAddressOf(), &stride, &offset);
            dc->IASetIndexBuffer(info.mesh->data->indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);

            // ワールド行列を更新（スキンメッシュ用シェーダーがない場合、とりあえずワールド行列で描く）
            // ※本来は CB_Skeleton を更新する必要があります
            shader->Update(rc, *info.mesh); // ここでWorld行列を送る実装になっているか確認が必要

            // 個別描画
            dc->DrawIndexed((UINT)info.mesh->data->indices.size(), 0, 0);
        }
        else {
            // === B. 静的メッシュ（背景・プロップ）：インスタンシング ===
            // 直前のバッチと同じメッシュなら追加、違えば新規作成
            if (!batches.empty() && batches.back().mesh == info.mesh) {
                batches.back().data.push_back({info.worldMatrix});
            }
            else {
                InstanceBatch newBatch;
                newBatch.mesh = info.mesh;
                newBatch.data.push_back({info.worldMatrix});
                batches.push_back(newBatch);
            }
        }
    }

    // --- インスタンシング描画実行 (静的メッシュ) ---
    for (const auto &batch : batches) {

        // =========================================================================
        // 静的メッシュでも必ずシェーダーのUpdateを呼び、定数バッファをGPUに送る
        // =========================================================================
        shader->Update(rc, *batch.mesh);

        size_t totalCount = batch.data.size();
        size_t offset     = 0;

        // インスタンスバッファの上限ごとに分割して描画
        while (offset < totalCount) {
            // 今回描画する数 (残り or 上限)
            size_t drawCount = (std::min)(totalCount - offset, InstanceBuffer::MAX_INSTANCES);

            // 必要な分だけデータをコピーするための部分配列を作成
            // (vectorのイテレータを使って範囲指定コピー)
            std::vector<InstanceData> chunk(
                batch.data.begin() + offset, batch.data.begin() + offset + drawCount);

            // GPUバッファに転送 (Map/Unmap)
            _instanceBuffer.Write(dc, chunk);

            // VSの指定スロット(t11等)にバインド
            _instanceBuffer.Bind(dc, SLOT_SRV_INSTANCE);

            // 頂点バッファセット
            UINT stride = sizeof(ModelResource::Vertex);
            UINT zero   = 0;

            dc->IASetVertexBuffers(
                0, 1, batch.mesh->data->vertexBuffer.GetAddressOf(), &stride, &zero);
            dc->IASetIndexBuffer(batch.mesh->data->indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);

            // インスタンシング描画発行
            // 第1引数: インデックス数
            // 第2引数: インスタンス数
            dc->DrawIndexedInstanced(
                (UINT)batch.mesh->data->indices.size(), (UINT)drawCount, 0, 0, 0);

            offset += drawCount;
        }
    }

    // 4. 終了処理
    // シェーダー終了
    shader->End(rc);

    // インスタンスバッファのバインド解除（重要）
    _instanceBuffer.Unbind(dc, SLOT_SRV_INSTANCE);

    // シャドウマップへの書き込み終了
    shadowMap->Deactivate(dc);
}