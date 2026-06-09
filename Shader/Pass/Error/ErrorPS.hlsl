// Shader/Core/ErrorPS.hlsl
// 一律マゼンタを出力。画面の紫＝「このメッシュのシェーダーが未登録」のサイン。
float4 main() : SV_TARGET
{
    return float4(1.0f, 0.0f, 1.0f, 1.0f); // magenta
}