// shader.hlsl

cbuffer CBTransform : register(b0)
{
    float4x4 WorldViewProj;
};

struct VS_INPUT {
    float3 pos : POSITION;
    float2 uv  : TEXCOORD;
};

struct VS_OUTPUT {
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD;
};

VS_OUTPUT VSMain(VS_INPUT input) {
    VS_OUTPUT o;
    o.pos = mul(float4(input.pos, 1.0f), WorldViewProj);
    o.uv  = input.uv;
    return o;
}

// 텍스처와 샘플러
Texture2D    g_Tex : register(t0);
SamplerState g_Sampler : register(s0);

float4 PSMain(VS_OUTPUT input) : SV_TARGET {
    // 텍스처가 있으면 텍스처 색상, 없으면 UV 좌표 기반 그라데이션
    float4 texColor = g_Tex.Sample(g_Sampler, input.uv);
    float4 gradientColor = float4(input.uv, 0.5f, 1.0f);
    return texColor;
}
