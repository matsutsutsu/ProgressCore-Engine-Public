#include "Shader/Common/Common.hlsli"

// --- リソース定義 ---
cbuffer CbMesh : register(b0)
{
    float4 materialColor;
};

SamplerState LinearSampler : register(s0);
Texture2D DiffuseMap : register(t0);
Texture2D NormalMap : register(t1);

// ディゾルブで使うノイズテクスチャ
Texture2D DissolveNoiseMap : register(REG_SRV_GLOBAL_NOISE);


// --- メイン関数 ---
float4 main(VS_OUT pin) : SV_TARGET
{
    // ------------------------------------------------------------
    // 0. ディゾルブ (消滅) 処理
    // ------------------------------------------------------------
    float dissolveThreshold = pin.customParams.x; // C++から送られてきた進行度 (0.0 ～ 1.0)
    float flashIntensity = pin.customParams.z; // C++から送られてきた進行度 (0.0 ～ 1.0)
    
    if (dissolveThreshold > 0.0f)
    {
        // ノイズテクスチャから白黒の値を読み取る (UVを少し掛けて細かさを調整)
        // 1.0が白、0.0が黒。
        float noiseScale = 0.1f;
    
        float noiseValue = DissolveNoiseMap.Sample(LinearSampler, pin.texcoord * noiseScale).r;
        
        // 【魔法の関数 clip】
        // 括弧の中が「0未満マイナス」になった瞬間、このピクセルは画面に描画されず強制終了(破棄)される。
        // つまり noiseValue が閾値を下回った黒い部分から順番に穴が空いていく。
        clip(noiseValue - dissolveThreshold);
    }
    
    
    // ------------------------------------------------------------
    // 1. 表面情報 (SurfaceInfo) の構築
    // ------------------------------------------------------------
    // ばらばらに届いた頂点データ(pin)を元に、ライティング計算に必要な情報をSurfaceInfo構造体にまとめる
    SurfaceInfo surf;

    // 基本色と透明度の取得
    // DiffuseMap.Sample() でテクスチャから色をサンプリングし、materialColor と乗算
    float4 sampledColor = DiffuseMap.Sample(LinearSampler, pin.texcoord) * materialColor;

    surf.color = sampledColor.rgb;
    surf.alpha = sampledColor.a;

    // ワールド座標の格納
    surf.pos = pin.position.xyz;

    // 視線ベクトルの計算 (カメラ座標 - ピクセル座標)
    // 光がどこで反射して目に届くかを計算するために必要
    surf.viewDir = normalize(cameraPosition.xyz - surf.pos);

    // 法線計算を関数に委譲
    // 法線マップを考慮した最終的な法線ベクトルを取得
    surf.normal = CalculateWorldNormal(
        pin.normal,
        pin.tangent,
        pin.texcoord,
        NormalMap,
        LinearSampler
    );
    
    // ------------------------------------------------------------
    // 2. 影と環境光の計算 (ライティングの準備)
    // ------------------------------------------------------------
    
    // シャドウマップから影の重みを計算 (1.0:日向, 0.0:影)
    float shadowWeight = CalculateShadowWeight(pin.shadowPos);
    
    // 半球ライティングによる環境光の計算
    // 完全に真っ暗な影を防ぐために、空からの光と地面からの反射光を合成
    float hemiWeight = surf.normal.y * 0.5f + 0.5f;
    float3 ambient = lerp(gDirLight.gGroundColor.rgb * gDirLight.gGroundColor.a, gDirLight.gSkyColor.rgb * gDirLight.gSkyColor.a, hemiWeight);
    
    
    // ------------------------------------------------------------
    // 3. 統合ライティングの実行
    // ------------------------------------------------------------
    // Lighting.hlsli に定義した関数で全ての直接光（平行/点/スポット）を一括計算
    // 平行光源、点光源、スポットライトの影響を受けた拡散反射と鏡面反射の色を取得
    LightResult light = CalculateAllLights(surf, shadowWeight);

    // ------------------------------------------------------------
    // 4. 最終色の合成
    // ------------------------------------------------------------
    // 拡散反射(diffuse)と環境光(ambient)を合算して物体色を乗算、最後に鏡面反射(specular)を足す
    float3 finalColor = surf.color * (light.diffuse + ambient) + light.specular;

    // 影の色（暗部への色味）を適用
    finalColor = ApplyShadowColor(finalColor, shadowWeight);
    

    // ------------------------------------------------------------
    // ★追加: リムライト (Rim Light)
    // ------------------------------------------------------------
    if (gFogRimParams.y > 0.0f) // Strength > 0 なら計算
    {
        // 1. 視線と法線の内積 (1.0=正面, 0.0=輪郭)
        float NdotV = saturate(dot(surf.normal, surf.viewDir));

        // 2. 反転して累乗 (フレネル反射の近似)
        // 輪郭(0.0)に近いほど 1.0 になり、powerが高いほど鋭くなる
        float rimFactor = pow(1.0f - NdotV, gFogRimParams.x);

        // x = power
        
       // 平らな面を除外する (Curvature Mask) 
        // fwidth: 隣のピクセルとの値の差を取得する関数
        // 平らな壁なら法線は変化しないので 0 になり、曲面なら > 0 になる
        float curvature = length(fwidth(surf.normal));

        // 感度調整: 値を増幅して 0~1 にクランプ
        // ※モデルの滑らかさに応じて 50.0f ～ 1000.0f くらいで調整
        // 低ポリゴンのフラットシェーディングだとキャラも消えるので注意
        float flatMask = saturate(curvature * 100.0f);

        // 追加: Y軸向き(床)をマスク
        // 法線のY成分が 0.9 以上（ほぼ真上）なら光らせない
        float floorMask = 1.0f - step(0.9f, surf.normal.y);

        // 全部掛け合わせる
        float3 rimLight = rimFactor * gFogRimColor.rgb * gFogRimParams.y * flatMask * floorMask;

        // 4. 加算合成 (発光表現)
        finalColor += rimLight;
    }
    
    // ------------------------------------------------------------
    // 5. 後処理 (フォグ)
    // ------------------------------------------------------------
  
   // 引数に surf.normal を追加
    finalColor = ApplyHeightFog(surf.pos, cameraPosition.xyz, finalColor, LinearSampler, surf.normal);
    
    
    
    // ヒットフラッシュの処理
    // C++のHitFlashSystemから送られてきたフラッシュの強度（0.0 ～ 0.8）
  
    // 光らせたい色（真っ白）
    float3 flashColor = float3(1.0, 1.0, 1.0);

    // 本来の色と真っ白を、flashIntensity の割合で混ぜ合わせる (lerp)
    finalColor.rgb = lerp(finalColor.rgb, flashColor, flashIntensity);
    
    
    // ==========================================================
    // ディゾルブのエッジ発光（燃え尽きる表現）
    // ==========================================================
    if (dissolveThreshold > 0.0f)
    {
        // 冒頭と同じノイズの値を再度取得 (UVのスケールは冒頭と合わせる)
        float noiseValue = DissolveNoiseMap.Sample(LinearSampler, pin.texcoord * 3.0f).r;

        // 境界線の幅（0.05 など、小さくすると線が細くなる）
        float edgeWidth = 0.05f;

        // ノイズ値と閾値の「差」が境界幅の中にいるピクセルなら、エッジとして判定
        if (noiseValue - dissolveThreshold < edgeWidth)
        {
            // 強烈なオレンジ色を設定（RGBの値を1.0より大きくすることで、Bloom等のポストプロセスで光り輝く）
            float3 edgeColor = float3(3.0f, 1.0f, 0.2f);

            // 境界線の部分だけ色を上書きする
            finalColor = edgeColor;
        }
    }
    
    // 最終的なピクセルカラーを返却
    return float4(finalColor, surf.alpha);
}