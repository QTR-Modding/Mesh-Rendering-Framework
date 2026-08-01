#include "RenderManager.h"

#include <DirectXMath.h>
#include <DirectXTex.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wincodec.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>

namespace
{
    template <class T>
    void ReleaseResource(T*& resource)
    {
        if (resource) {
            resource->Release();
            resource = nullptr;
        }
    }

    void DiscardCommands(ID3D11DeviceContext* deferredContext)
    {
        if (!deferredContext) {
            return;
        }

        ID3D11CommandList* commandList = nullptr;
        if (SUCCEEDED(deferredContext->FinishCommandList(FALSE, &commandList))) {
            ReleaseResource(commandList);
        } else {
            deferredContext->ClearState();
        }
    }

    struct SceneConstants {
        DirectX::XMFLOAT4X4 worldViewProjection;
        DirectX::XMFLOAT4X4 world;
        DirectX::XMFLOAT4 cameraPosition;
        DirectX::XMFLOAT4 lightDirections[3];
        DirectX::XMFLOAT4 lightColors[3];
        DirectX::XMFLOAT4 ambientColor;
    };

    constexpr const char* vertexShaderSource = R"(
        cbuffer SceneConstants : register(b0)
        {
            row_major float4x4 worldViewProjection;
            row_major float4x4 world;
        };

        struct VertexInput
        {
            float3 position : POSITION;
            float3 normal : NORMAL;
            float4 tangent : TANGENT;
            float2 uv : TEXCOORD0;
            float4 color : COLOR0;
        };

        struct PixelInput
        {
            float4 position : SV_POSITION;
            float3 worldPosition : TEXCOORD1;
            float3 normal : NORMAL;
            float4 tangent : TANGENT;
            float2 uv : TEXCOORD0;
            float4 color : COLOR0;
        };

        PixelInput main(VertexInput input)
        {
            PixelInput output;
            output.position = mul(float4(input.position, 1.0f), worldViewProjection);
            output.worldPosition = mul(float4(input.position, 1.0f), world).xyz;
            output.normal = normalize(mul(input.normal, (float3x3)world));
            output.tangent.xyz = normalize(mul(input.tangent.xyz, (float3x3)world));
            output.tangent.w = input.tangent.w;
            output.uv = input.uv;
            output.color = input.color;
            return output;
        }
    )";

    constexpr const char* pixelShaderSource = R"(
        Texture2D diffuseTexture : register(t0);
        Texture2D normalTexture : register(t1);
        Texture2D auxiliaryTexture : register(t2);
        Texture2D heightTexture : register(t3);
        TextureCube environmentTexture : register(t4);
        Texture2D environmentMaskTexture : register(t5);
        Texture2D subsurfaceTexture : register(t6);
        Texture2D backlightTexture : register(t7);
        Texture2D specularTexture : register(t8);
        SamplerState materialSampler : register(s0);

        cbuffer SceneConstants : register(b0)
        {
            row_major float4x4 worldViewProjection;
            row_major float4x4 world;
            float4 cameraPosition;
            float4 lightDirections[3];
            float4 lightColors[3];
            float4 ambientColor;
        };

        cbuffer MaterialConstants : register(b1)
        {
            float alphaThreshold;
            float materialAlpha;
            float blendEnabled;
            float specularStrength;
            float3 specularColor;
            float glossiness;
            float3 emissiveColor;
            float emissiveMultiple;
            float3 tintColor;
            float environmentScale;
            float2 uvScale;
            float2 uvOffset;
            float parallaxScale;
            float backlightPower;
            float rimlightPower;
            float auxiliaryMapMode;
            float hasNormalMap;
            float hasHeightMap;
            float hasEnvironmentMap;
            float hasEnvironmentMask;
            float hasSubsurfaceMap;
            float hasBacklightMap;
            float hasSpecularMap;
            float modelSpaceNormals;
            float useVertexColors;
            float useVertexAlpha;
            float emissiveEnabled;
            float specularEnabled;
            float hasAuxiliaryMap;
            float3 materialPadding;
        };

        struct PixelInput
        {
            float4 position : SV_POSITION;
            float3 worldPosition : TEXCOORD1;
            float3 normal : NORMAL;
            float4 tangent : TANGENT;
            float2 uv : TEXCOORD0;
            float4 color : COLOR0;
        };

        float3 EvaluateLight(
            float3 normal,
            float3 viewDirection,
            float3 albedo,
            float specularMask,
            uint lightIndex)
        {
            float3 lightDirection = normalize(lightDirections[lightIndex].xyz);
            float diffuseAmount = saturate(dot(normal, lightDirection));
            float3 halfVector = normalize(lightDirection + viewDirection);
            float specularAmount = pow(saturate(dot(normal, halfVector)), max(glossiness, 1.0f));
            float3 diffuseLight = albedo * diffuseAmount;
            float3 specularLight = specularColor * specularStrength * specularMask * specularAmount;
            return (diffuseLight + specularLight) * lightColors[lightIndex].rgb;
        }

        float4 main(PixelInput input) : SV_TARGET
        {
            float3 geometricNormal = normalize(input.normal);
            float3 tangent = normalize(input.tangent.xyz - geometricNormal * dot(input.tangent.xyz, geometricNormal));
            float3 bitangent = normalize(cross(geometricNormal, tangent)) * input.tangent.w;
            float3x3 tangentToWorld = float3x3(tangent, bitangent, geometricNormal);
            float3 viewDirection = normalize(cameraPosition.xyz - input.worldPosition);

            float2 uv = input.uv * uvScale + uvOffset;
            if (hasHeightMap > 0.5f && parallaxScale > 0.0f) {
                float3 tangentView = mul(tangentToWorld, viewDirection);
                float height = heightTexture.Sample(materialSampler, uv).r - 0.5f;
                uv -= tangentView.xy * (height * parallaxScale / max(abs(tangentView.z), 0.2f));
            }

            float4 diffuseSample = diffuseTexture.Sample(materialSampler, uv);
            float3 vertexRgb = lerp(float3(1.0f, 1.0f, 1.0f), input.color.rgb, useVertexColors);
            float vertexAlpha = lerp(1.0f, input.color.a, useVertexAlpha);
            float3 albedo = diffuseSample.rgb * vertexRgb;
            float alpha = diffuseSample.a * vertexAlpha * materialAlpha;
            clip(alpha - alphaThreshold);

            float4 normalSample = normalTexture.Sample(materialSampler, uv);
            float3 normal = geometricNormal;
            if (hasNormalMap > 0.5f) {
                if (modelSpaceNormals > 0.5f) {
                    float3 mappedNormal = normalize(normalSample.xyz * 2.0f - 1.0f);
                    normal = normalize(mul(mappedNormal, (float3x3)world));
                } else {
                    float2 mappedXY = normalSample.xy * 2.0f - 1.0f;
                    float mappedZ = sqrt(saturate(1.0f - dot(mappedXY, mappedXY)));
                    normal = normalize(mul(float3(mappedXY, mappedZ), tangentToWorld));
                }
            }

            float4 auxiliary = auxiliaryTexture.Sample(materialSampler, uv);
            if (auxiliaryMapMode > 1.5f && auxiliaryMapMode < 2.5f) {
                albedo *= lerp(float3(1.0f, 1.0f, 1.0f), tintColor, auxiliary.r);
            } else if (auxiliaryMapMode > 3.5f && hasSubsurfaceMap > 0.5f) {
                float grayscaleMask = subsurfaceTexture.Sample(materialSampler, uv).r;
                albedo *= lerp(float3(1.0f, 1.0f, 1.0f), tintColor, grayscaleMask);
            }

            float specularMask = hasSpecularMap > 0.5f
                ? specularTexture.Sample(materialSampler, uv).r
                : (hasNormalMap > 0.5f ? normalSample.a : 1.0f);
            specularMask *= specularEnabled;

            float3 color = albedo * ambientColor.rgb;
            color += EvaluateLight(normal, viewDirection, albedo, specularMask, 0);
            color += EvaluateLight(normal, viewDirection, albedo, specularMask, 1);
            color += EvaluateLight(normal, viewDirection, albedo, specularMask, 2);

            float backFacingLight = saturate(dot(-normal, normalize(lightDirections[0].xyz)));
            if (hasSubsurfaceMap > 0.5f && auxiliaryMapMode < 3.5f) {
                color += subsurfaceTexture.Sample(materialSampler, uv).rgb * albedo * backFacingLight * 0.35f;
            }
            if (hasBacklightMap > 0.5f) {
                color += backlightTexture.Sample(materialSampler, uv).rgb * backFacingLight * max(backlightPower, 0.25f);
            }

            float rim = pow(1.0f - saturate(dot(normal, viewDirection)), max(rimlightPower, 0.01f));
            if (auxiliaryMapMode > 2.5f) {
                color += auxiliary.rgb * albedo * rim * 0.3f;
            }
            if (emissiveEnabled > 0.5f) {
                float useGlowTexture = hasAuxiliaryMap *
                    (((auxiliaryMapMode > 0.5f && auxiliaryMapMode < 1.5f) || auxiliaryMapMode > 3.5f) ? 1.0f : 0.0f);
                float3 glow = useGlowTexture > 0.5f
                    ? auxiliary.rgb
                    : float3(1.0f, 1.0f, 1.0f);
                color += glow * emissiveColor * emissiveMultiple;
            }

            if (hasEnvironmentMap > 0.5f) {
                float3 reflectionDirection = reflect(-viewDirection, normal);
                float3 environment = environmentTexture.Sample(materialSampler, reflectionDirection).rgb;
                float environmentMask = hasEnvironmentMask > 0.5f
                    ? environmentMaskTexture.Sample(materialSampler, uv).r
                    : 1.0f;
                float fresnel = pow(1.0f - saturate(dot(normal, viewDirection)), 5.0f);
                color += environment * environmentMask * environmentScale * lerp(0.25f, 1.0f, fresnel);
            }

            float outputAlpha = blendEnabled > 0.5f ? alpha : 1.0f;
            // Lift UI midtones without applying a per-channel tone curve, which
            // would wash out the material colors. Scaling by luminance preserves hue.
            float3 positiveColor = max(color, 0.0f);
            float luminance = max(dot(positiveColor, float3(0.2126f, 0.7152f, 0.0722f)), 0.00001f);
            float displayLuminance = pow(saturate(luminance), 1.0f / 1.5f);
            float3 displayColor = saturate(positiveColor * (displayLuminance / luminance));
            return float4(displayColor, outputAlpha);
        }
    )";

    bool CompileShader(const char* source, const char* target, ID3DBlob** byteCode)
    {
        ID3DBlob* errors = nullptr;
        const HRESULT result = D3DCompile(
            source,
            std::strlen(source),
            nullptr,
            nullptr,
            nullptr,
            "main",
            target,
            D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
            0,
            byteCode,
            &errors);

        if (FAILED(result) && errors) {
            logger::error("Mesh shader compilation failed: {}", static_cast<const char*>(errors->GetBufferPointer()));
        }
        ReleaseResource(errors);
        return SUCCEEDED(result);
    }

    HRESULT CreateSolidTexture(
        ID3D11Device* device,
        std::uint32_t pixel,
        bool cubeTexture,
        ID3D11ShaderResourceView** textureView)
    {
        if (!device || !textureView) {
            return E_INVALIDARG;
        }

        D3D11_TEXTURE2D_DESC description{};
        description.Width = 1;
        description.Height = 1;
        description.MipLevels = 1;
        description.ArraySize = cubeTexture ? 6 : 1;
        description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        description.SampleDesc.Count = 1;
        description.Usage = D3D11_USAGE_IMMUTABLE;
        description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        description.MiscFlags = cubeTexture ? D3D11_RESOURCE_MISC_TEXTURECUBE : 0;

        std::array<D3D11_SUBRESOURCE_DATA, 6> initialData{};
        for (D3D11_SUBRESOURCE_DATA& subresource : initialData) {
            subresource.pSysMem = &pixel;
            subresource.SysMemPitch = sizeof(pixel);
        }

        ID3D11Texture2D* texture = nullptr;
        HRESULT result = device->CreateTexture2D(&description, initialData.data(), &texture);
        if (SUCCEEDED(result)) {
            result = device->CreateShaderResourceView(texture, nullptr, textureView);
        }
        ReleaseResource(texture);
        return result;
    }

    DirectX::XMMATRIX GetWorldMatrix(const MeshRenderingFrameworkAPI::Internal::IMesh& mesh)
    {
        const RE::NiMatrix3& rotation = mesh.rotation;
        const DirectX::XMMATRIX rotationMatrix = DirectX::XMMatrixSet(
            rotation.entry[0][0], rotation.entry[1][0], rotation.entry[2][0], 0.0f,
            rotation.entry[0][1], rotation.entry[1][1], rotation.entry[2][1], 0.0f,
            rotation.entry[0][2], rotation.entry[1][2], rotation.entry[2][2], 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f);

        return DirectX::XMMatrixScaling(mesh.scale, mesh.scale, mesh.scale) *
               rotationMatrix *
               DirectX::XMMatrixTranslation(mesh.position.x, mesh.position.y, mesh.position.z);
    }
}

MeshRenderingFrameworkAPI::Internal::IMesh* RenderManager::AddByNifPAth(
    const char* nifPath, uint32_t width, uint32_t height)
{
    if (!nifPath || !nifPath[0] || width == 0 || height == 0) {
        return nullptr;
    }

    std::unique_lock lock(mutex);
    Mesh* loadedMesh = new Mesh(nifPath, width, height);
    if (!loadedMesh->IsValid()) {
        delete loadedMesh;
        return nullptr;
    }

    meshes[loadedMesh->mesh] = loadedMesh;
    return loadedMesh->mesh;
}

MeshRenderingFrameworkAPI::Internal::IMesh* RenderManager::AddByNiAVObjectList(
    RE::NiAVObject* const*, uint32_t, uint32_t, uint32_t)
{
    logger::error("NiAVObject rendering is unavailable: nifly requires a source NIF stream");
    return nullptr;
}

bool RenderManager::CopyRenderTargetToMesh(Mesh* sourceMesh, RenderTarget* target)
{
    if (!sourceMesh || !sourceMesh->mesh || !target || !target->texture || !device || !renderContext) {
        return false;
    }

    MeshRenderingFrameworkAPI::Internal::IMesh* outputMesh = sourceMesh->mesh;
    D3D11_TEXTURE2D_DESC targetDescription{};
    target->texture->GetDesc(&targetDescription);

    bool needsTexture = !outputMesh->texture || !outputMesh->SRV;
    if (outputMesh->texture) {
        D3D11_TEXTURE2D_DESC currentDescription{};
        outputMesh->texture->GetDesc(&currentDescription);
        needsTexture = needsTexture || currentDescription.Width != targetDescription.Width ||
                       currentDescription.Height != targetDescription.Height ||
                       currentDescription.Format != targetDescription.Format;
    }

    if (needsTexture) {
        ReleaseResource(outputMesh->SRV);
        ReleaseResource(outputMesh->texture);

        D3D11_TEXTURE2D_DESC outputDescription = targetDescription;
        outputDescription.Usage = D3D11_USAGE_DEFAULT;
        outputDescription.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        outputDescription.CPUAccessFlags = 0;
        outputDescription.MiscFlags = 0;

        HRESULT result = device->CreateTexture2D(&outputDescription, nullptr, &outputMesh->texture);
        if (FAILED(result)) {
            return false;
        }
        result = device->CreateShaderResourceView(outputMesh->texture, nullptr, &outputMesh->SRV);
        if (FAILED(result)) {
            ReleaseResource(outputMesh->texture);
            return false;
        }
    }

    renderContext->CopyResource(outputMesh->texture, target->texture);
    return true;
}

bool RenderManager::InitializePipeline()
{
    if (vertexShader && pixelShader && inputLayout && constantBuffer && samplerState &&
        materialConstantBuffer && rasterizerState && opaqueBlendState && alphaBlendState &&
        depthWriteState && depthReadState && fallbackWhiteTexture && fallbackNormalTexture &&
        fallbackBlackTexture && fallbackEnvironmentTexture) {
        return true;
    }
    if (!device) {
        return false;
    }

    ReleasePipeline();
    ID3DBlob* vertexByteCode = nullptr;
    ID3DBlob* pixelByteCode = nullptr;
    if (!CompileShader(vertexShaderSource, "vs_5_0", &vertexByteCode) ||
        !CompileShader(pixelShaderSource, "ps_5_0", &pixelByteCode)) {
        ReleaseResource(vertexByteCode);
        ReleaseResource(pixelByteCode);
        return false;
    }

    HRESULT result = device->CreateVertexShader(
        vertexByteCode->GetBufferPointer(), vertexByteCode->GetBufferSize(), nullptr, &vertexShader);
    if (SUCCEEDED(result)) {
        result = device->CreatePixelShader(
            pixelByteCode->GetBufferPointer(), pixelByteCode->GetBufferSize(), nullptr, &pixelShader);
    }

    const D3D11_INPUT_ELEMENT_DESC elements[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(MeshVertex, position), D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(MeshVertex, normal), D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(MeshVertex, tangent), D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(MeshVertex, uv), D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(MeshVertex, color), D3D11_INPUT_PER_VERTEX_DATA, 0}
    };
    if (SUCCEEDED(result)) {
        result = device->CreateInputLayout(
            elements,
            static_cast<UINT>(std::size(elements)),
            vertexByteCode->GetBufferPointer(),
            vertexByteCode->GetBufferSize(),
            &inputLayout);
    }
    ReleaseResource(vertexByteCode);
    ReleaseResource(pixelByteCode);

    D3D11_BUFFER_DESC constantDescription{};
    constantDescription.ByteWidth = sizeof(SceneConstants);
    constantDescription.Usage = D3D11_USAGE_DYNAMIC;
    constantDescription.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    constantDescription.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (SUCCEEDED(result)) {
        result = device->CreateBuffer(&constantDescription, nullptr, &constantBuffer);
    }

    D3D11_BUFFER_DESC materialConstantDescription{};
    materialConstantDescription.ByteWidth = sizeof(MeshMaterialConstants);
    materialConstantDescription.Usage = D3D11_USAGE_DYNAMIC;
    materialConstantDescription.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    materialConstantDescription.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (SUCCEEDED(result)) {
        result = device->CreateBuffer(&materialConstantDescription, nullptr, &materialConstantBuffer);
    }

    D3D11_SAMPLER_DESC samplerDescription{};
    samplerDescription.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDescription.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    samplerDescription.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    samplerDescription.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    samplerDescription.MaxLOD = D3D11_FLOAT32_MAX;
    if (SUCCEEDED(result)) {
        result = device->CreateSamplerState(&samplerDescription, &samplerState);
    }

    D3D11_RASTERIZER_DESC rasterizerDescription{};
    rasterizerDescription.FillMode = D3D11_FILL_SOLID;
    rasterizerDescription.CullMode = D3D11_CULL_NONE;
    rasterizerDescription.DepthClipEnable = TRUE;
    rasterizerDescription.ScissorEnable = TRUE;
    if (SUCCEEDED(result)) {
        result = device->CreateRasterizerState(&rasterizerDescription, &rasterizerState);
    }

    D3D11_BLEND_DESC opaqueBlendDescription{};
    opaqueBlendDescription.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (SUCCEEDED(result)) {
        result = device->CreateBlendState(&opaqueBlendDescription, &opaqueBlendState);
    }

    D3D11_BLEND_DESC alphaBlendDescription{};
    D3D11_RENDER_TARGET_BLEND_DESC& targetBlend = alphaBlendDescription.RenderTarget[0];
    targetBlend.BlendEnable = TRUE;
    targetBlend.SrcBlend = D3D11_BLEND_SRC_ALPHA;
    targetBlend.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    targetBlend.BlendOp = D3D11_BLEND_OP_ADD;
    targetBlend.SrcBlendAlpha = D3D11_BLEND_ONE;
    targetBlend.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    targetBlend.BlendOpAlpha = D3D11_BLEND_OP_ADD;
    targetBlend.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (SUCCEEDED(result)) {
        result = device->CreateBlendState(&alphaBlendDescription, &alphaBlendState);
    }

    D3D11_DEPTH_STENCIL_DESC depthWriteDescription{};
    depthWriteDescription.DepthEnable = TRUE;
    depthWriteDescription.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    depthWriteDescription.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    if (SUCCEEDED(result)) {
        result = device->CreateDepthStencilState(&depthWriteDescription, &depthWriteState);
    }

    D3D11_DEPTH_STENCIL_DESC depthReadDescription = depthWriteDescription;
    depthReadDescription.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    if (SUCCEEDED(result)) {
        result = device->CreateDepthStencilState(&depthReadDescription, &depthReadState);
    }

    if (SUCCEEDED(result)) {
        result = CreateSolidTexture(device, 0xFFFFFFFF, false, &fallbackWhiteTexture);
    }
    if (SUCCEEDED(result)) {
        result = CreateSolidTexture(device, 0xFFFF8080, false, &fallbackNormalTexture);
    }
    if (SUCCEEDED(result)) {
        result = CreateSolidTexture(device, 0xFF000000, false, &fallbackBlackTexture);
    }
    if (SUCCEEDED(result)) {
        result = CreateSolidTexture(device, 0xFF000000, true, &fallbackEnvironmentTexture);
    }

    if (FAILED(result)) {
        ReleasePipeline();
        return false;
    }
    return true;
}

void RenderManager::ReleasePipeline()
{
    ReleaseResource(fallbackEnvironmentTexture);
    ReleaseResource(fallbackBlackTexture);
    ReleaseResource(fallbackNormalTexture);
    ReleaseResource(fallbackWhiteTexture);
    ReleaseResource(depthReadState);
    ReleaseResource(depthWriteState);
    ReleaseResource(alphaBlendState);
    ReleaseResource(opaqueBlendState);
    ReleaseResource(rasterizerState);
    ReleaseResource(samplerState);
    ReleaseResource(materialConstantBuffer);
    ReleaseResource(constantBuffer);
    ReleaseResource(inputLayout);
    ReleaseResource(pixelShader);
    ReleaseResource(vertexShader);
}

bool RenderManager::RenderMesh(Mesh* sourceMesh, RenderTarget* target)
{
    if (!sourceMesh || !sourceMesh->IsValid() || !target || !target->initialized ||
        !renderContext || !InitializePipeline() || !sourceMesh->InitializeGpuResources(device)) {
        return false;
    }

    ID3D11RenderTargetView* renderTargetView = target->renderTargetView;
    renderContext->OMSetRenderTargets(1, &renderTargetView, target->depthStencilView);
    renderContext->OMSetBlendState(opaqueBlendState, nullptr, 0xFFFFFFFF);
    renderContext->OMSetDepthStencilState(depthWriteState, 0);
    renderContext->RSSetState(rasterizerState);

    const D3D11_VIEWPORT viewport{
        0.0f,
        0.0f,
        static_cast<float>(target->width),
        static_cast<float>(target->height),
        0.0f,
        1.0f
    };
    const D3D11_RECT scissor{
        0,
        0,
        static_cast<LONG>(target->width),
        static_cast<LONG>(target->height)
    };
    renderContext->RSSetViewports(1, &viewport);
    renderContext->RSSetScissorRects(1, &scissor);

    renderContext->IASetInputLayout(inputLayout);
    renderContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    renderContext->VSSetShader(vertexShader, nullptr, 0);
    renderContext->PSSetShader(pixelShader, nullptr, 0);
    renderContext->GSSetShader(nullptr, nullptr, 0);
    renderContext->HSSetShader(nullptr, nullptr, 0);
    renderContext->DSSetShader(nullptr, nullptr, 0);
    renderContext->VSSetConstantBuffers(0, 1, &constantBuffer);
    renderContext->PSSetConstantBuffers(0, 1, &constantBuffer);
    renderContext->PSSetConstantBuffers(1, 1, &materialConstantBuffer);
    renderContext->PSSetSamplers(0, 1, &samplerState);

    const float clearColor[4]{0.0f, 0.0f, 0.0f, 0.0f};
    renderContext->ClearRenderTargetView(target->renderTargetView, clearColor);
    renderContext->ClearDepthStencilView(target->depthStencilView, D3D11_CLEAR_DEPTH, 1.0f, 0);

    const float aspect = static_cast<float>(target->width) / static_cast<float>(target->height);
    constexpr float cameraY = 320.0f;
    constexpr float subjectY = -500.0f;
    constexpr float horizontalHalfSpan = 130.0f;
    constexpr float cameraDistance = cameraY - subjectY;
    const float verticalHalfSpan = horizontalHalfSpan / aspect;

    const DirectX::XMMATRIX world = GetWorldMatrix(*sourceMesh->mesh);
    const DirectX::XMMATRIX view = DirectX::XMMatrixLookAtRH(
        DirectX::XMVectorSet(0.0f, cameraY, 0.0f, 1.0f),
        DirectX::XMVectorSet(0.0f, subjectY, 0.0f, 1.0f),
        DirectX::XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f));
    const DirectX::XMMATRIX projection = DirectX::XMMatrixPerspectiveFovRH(
        2.0f * std::atan(verticalHalfSpan / cameraDistance), aspect, 1.0f, 10000.0f);

    SceneConstants constants{};
    DirectX::XMStoreFloat4x4(&constants.world, world);
    DirectX::XMStoreFloat4x4(
        &constants.worldViewProjection,
        world * view * projection);
    constants.cameraPosition = DirectX::XMFLOAT4(0.0f, cameraY, 0.0f, 1.0f);
    constants.lightDirections[0] = DirectX::XMFLOAT4(-0.45f, 0.55f, 0.70f, 0.0f);
    constants.lightDirections[1] = DirectX::XMFLOAT4(0.65f, 0.35f, 0.25f, 0.0f);
    constants.lightDirections[2] = DirectX::XMFLOAT4(0.10f, -0.65f, 0.55f, 0.0f);
    constants.lightColors[0] = DirectX::XMFLOAT4(1.05f, 0.98f, 0.88f, 1.0f);
    constants.lightColors[1] = DirectX::XMFLOAT4(0.42f, 0.50f, 0.62f, 1.0f);
    constants.lightColors[2] = DirectX::XMFLOAT4(0.30f, 0.35f, 0.42f, 1.0f);
    constants.ambientColor = DirectX::XMFLOAT4(0.30f, 0.32f, 0.36f, 1.0f);

    std::array<ID3D11ShaderResourceView*, MeshTextureSlotCount> fallbackTextures{
        fallbackWhiteTexture,
        fallbackNormalTexture,
        fallbackBlackTexture,
        fallbackBlackTexture,
        fallbackEnvironmentTexture,
        fallbackWhiteTexture,
        fallbackBlackTexture,
        fallbackBlackTexture,
        fallbackBlackTexture
    };

    D3D11_MAPPED_SUBRESOURCE mapped{};
    bool rendered = false;
    if (SUCCEEDED(renderContext->Map(constantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        std::memcpy(mapped.pData, &constants, sizeof(constants));
        renderContext->Unmap(constantBuffer, 0);
        sourceMesh->Draw(renderContext, fallbackTextures.data(), materialConstantBuffer, false);
        renderContext->OMSetBlendState(alphaBlendState, nullptr, 0xFFFFFFFF);
        renderContext->OMSetDepthStencilState(depthReadState, 0);
        sourceMesh->Draw(renderContext, fallbackTextures.data(), materialConstantBuffer, true);
        renderContext->OMSetRenderTargets(0, nullptr, nullptr);
        rendered = CopyRenderTargetToMesh(sourceMesh, target);
    }

    std::array<ID3D11ShaderResourceView*, MeshTextureSlotCount> noTextures{};
    renderContext->PSSetShaderResources(0, static_cast<UINT>(noTextures.size()), noTextures.data());
    if (!rendered) {
        DiscardCommands(renderContext);
        return false;
    }
    return ExecuteCommands();
}

bool RenderManager::ExecuteCommands()
{
    if (!renderContext || !immediateContext || !completionQuery) {
        return false;
    }

    ID3D11CommandList* commandList = nullptr;
    const HRESULT finishResult = renderContext->FinishCommandList(FALSE, &commandList);
    if (FAILED(finishResult) || !commandList) {
        renderContext->ClearState();
        logger::error("Could not finish mesh render command list: {:08X}", static_cast<std::uint32_t>(finishResult));
        return false;
    }

    RE::BSGraphics::Renderer* renderer = RE::BSGraphics::Renderer::GetSingleton();
    if (!renderer) {
        commandList->Release();
        return false;
    }

    renderer->Lock();
    // TRUE restores every immediate-context state slot after the private
    // command list has executed.
    immediateContext->ExecuteCommandList(commandList, TRUE);
    commandList->Release();

    immediateContext->End(completionQuery);
    HRESULT completionResult = S_FALSE;
    while (completionResult == S_FALSE) {
        completionResult = immediateContext->GetData(completionQuery, nullptr, 0, 0);
        if (completionResult == S_FALSE) {
            SwitchToThread();
        }
    }
    renderer->Unlock();
    return SUCCEEDED(completionResult);
}

bool RenderManager::RenderLocked(MeshRenderingFrameworkAPI::Internal::IMesh* outputMesh)
{
    if (!outputMesh || !device || !renderContext || !immediateContext) {
        return false;
    }

    std::map<MeshRenderingFrameworkAPI::Internal::IMesh*, Mesh*>::iterator meshIterator = meshes.find(outputMesh);
    if (meshIterator == meshes.end() || !meshIterator->second) {
        return false;
    }
    if (!outputMesh->mustUpdate && !outputMesh->alwaysUpdate) {
        return outputMesh->SRV != nullptr;
    }

    const std::string targetKey = RenderTarget::GetKey(outputMesh->width, outputMesh->height);
    std::map<std::string, RenderTarget*>::iterator targetIterator = renderTarget.find(targetKey);
    if (targetIterator == renderTarget.end() || !targetIterator->second) {
        return false;
    }

    RenderTarget* target = targetIterator->second;
    if (!target->initialized) {
        InitRenderTarget(target);
    }
    if (!target->initialized || !RenderMesh(meshIterator->second, target)) {
        return false;
    }

    outputMesh->mustUpdate = false;
    return true;
}

bool RenderManager::Render(MeshRenderingFrameworkAPI::Internal::IMesh* outputMesh)
{
    std::unique_lock lock(mutex);
    return RenderLocked(outputMesh);
}

void RenderManager::RenderPending()
{
    std::unique_lock lock(mutex);
    std::map<MeshRenderingFrameworkAPI::Internal::IMesh*, Mesh*>::iterator meshIterator = meshes.begin();
    while (meshIterator != meshes.end()) {
        MeshRenderingFrameworkAPI::Internal::IMesh* outputMesh = meshIterator->first;
        ++meshIterator;
        if (!outputMesh) {
            continue;
        }

        if (outputMesh->mustUpdate || outputMesh->alwaysUpdate) {
            RenderLocked(outputMesh);
        }

        if (outputMesh->saveNextFrame && outputMesh->savePath && SaveLocked(outputMesh, outputMesh->savePath)) {
            outputMesh->savePath = nullptr;
            outputMesh->saveNextFrame = false;
        }
        if (outputMesh->deleteAfterSave && !outputMesh->saveNextFrame) {
            DeleteLocked(outputMesh);
        }
    }
}

void RenderManager::DeleteLocked(MeshRenderingFrameworkAPI::Internal::IMesh* outputMesh)
{
    std::map<MeshRenderingFrameworkAPI::Internal::IMesh*, Mesh*>::iterator meshIterator = meshes.find(outputMesh);
    if (meshIterator == meshes.end()) {
        return;
    }

    std::map<std::string, RenderTarget*>::iterator targetIterator;
    targetIterator = renderTarget.find(RenderTarget::GetKey(outputMesh->width, outputMesh->height));
    if (targetIterator != renderTarget.end()) {
        targetIterator->second->numReferences--;
        if (targetIterator->second->numReferences == 0) {
            delete targetIterator->second;
            renderTarget.erase(targetIterator);
        }
    }

    Mesh* sourceMesh = meshIterator->second;
    meshes.erase(meshIterator);
    delete sourceMesh;
}

void RenderManager::Delete(MeshRenderingFrameworkAPI::Internal::IMesh* outputMesh)
{
    if (!outputMesh) {
        return;
    }

    std::unique_lock lock(mutex);
    DeleteLocked(outputMesh);
}

void RenderManager::InitRenderTarget(RenderTarget* target)
{
    if (!target || !device || target->width == 0 || target->height == 0) {
        return;
    }

    target->initialized = false;
    ReleaseResource(target->depthStencilView);
    ReleaseResource(target->depthTexture);
    ReleaseResource(target->renderTargetView);
    ReleaseResource(target->texture);

    D3D11_TEXTURE2D_DESC colorDescription{};
    colorDescription.Width = target->width;
    colorDescription.Height = target->height;
    colorDescription.MipLevels = 1;
    colorDescription.ArraySize = 1;
    colorDescription.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    colorDescription.SampleDesc.Count = 1;
    colorDescription.Usage = D3D11_USAGE_DEFAULT;
    colorDescription.BindFlags = D3D11_BIND_RENDER_TARGET;

    HRESULT result = device->CreateTexture2D(&colorDescription, nullptr, &target->texture);
    if (SUCCEEDED(result)) {
        result = device->CreateRenderTargetView(target->texture, nullptr, &target->renderTargetView);
    }

    D3D11_TEXTURE2D_DESC depthDescription = colorDescription;
    depthDescription.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthDescription.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    if (SUCCEEDED(result)) {
        result = device->CreateTexture2D(&depthDescription, nullptr, &target->depthTexture);
    }
    if (SUCCEEDED(result)) {
        result = device->CreateDepthStencilView(target->depthTexture, nullptr, &target->depthStencilView);
    }

    if (FAILED(result)) {
        ReleaseResource(target->depthStencilView);
        ReleaseResource(target->depthTexture);
        ReleaseResource(target->renderTargetView);
        ReleaseResource(target->texture);
        return;
    }
    target->initialized = true;
}

bool RenderManager::Init(ID3D11Device* newDevice, ID3D11DeviceContext* newContext)
{
    std::unique_lock lock(mutex);

    ReleasePipeline();
    ReleaseResource(completionQuery);
    ReleaseResource(renderContext);
    device = newDevice;
    immediateContext = newContext;

    if (!device || !immediateContext) {
        return false;
    }

    HRESULT result = device->CreateDeferredContext(0, &renderContext);
    D3D11_QUERY_DESC queryDescription{};
    queryDescription.Query = D3D11_QUERY_EVENT;
    if (SUCCEEDED(result)) {
        result = device->CreateQuery(&queryDescription, &completionQuery);
    }
    if (FAILED(result) || !InitializePipeline()) {
        ReleasePipeline();
        ReleaseResource(completionQuery);
        ReleaseResource(renderContext);
        return false;
    }

    for (const std::pair<const std::string, RenderTarget*>& entry : renderTarget) {
        RenderTarget* target = entry.second;
        target->initialized = false;
        ReleaseResource(target->depthStencilView);
        ReleaseResource(target->depthTexture);
        ReleaseResource(target->renderTargetView);
        ReleaseResource(target->texture);
    }
    for (const std::pair<MeshRenderingFrameworkAPI::Internal::IMesh* const, Mesh*>& entry : meshes) {
        entry.second->ResetGpuResources();
    }
    return true;
}

bool RenderManager::SaveLocked(MeshRenderingFrameworkAPI::Internal::IMesh* mesh, const char* filename)
{
    if (!mesh || !filename || !filename[0]) {
        return false;
    }

    if (!RenderLocked(mesh) || !mesh->SRV || !device || !immediateContext) {
        return false;
    }

    std::filesystem::path path(filename);
    ID3D11Resource* resource = nullptr;
    mesh->SRV->GetResource(&resource);
    if (!resource) {
        return false;
    }

    DirectX::ScratchImage image;
    RE::BSGraphics::Renderer* renderer = RE::BSGraphics::Renderer::GetSingleton();
    if (!renderer) {
        resource->Release();
        return false;
    }

    renderer->Lock();
    const HRESULT captureResult = DirectX::CaptureTexture(device, immediateContext, resource, image);
    renderer->Unlock();
    if (FAILED(captureResult)) {
        resource->Release();
        return false;
    }

    const std::wstring wideName = path.wstring();
    HRESULT saveResult = E_FAIL;
    if (_wcsicmp(path.extension().c_str(), L".dds") == 0) {
        saveResult = DirectX::SaveToDDSFile(
            image.GetImages(), image.GetImageCount(), image.GetMetadata(), DirectX::DDS_FLAGS_NONE, wideName.c_str());
    } else {
        saveResult = DirectX::SaveToWICFile(
            *image.GetImage(0, 0, 0), DirectX::WIC_FLAGS_FORCE_SRGB, GUID_ContainerFormatPng, wideName.c_str());
    }
    resource->Release();
    return SUCCEEDED(saveResult);
}

bool RenderManager::Save(MeshRenderingFrameworkAPI::Internal::IMesh* mesh, const char* filename)
{
    std::unique_lock lock(mutex);
    return SaveLocked(mesh, filename);
}

std::string RenderTarget::GetKey(uint32_t width, uint32_t height)
{
    return std::format("{}x{}", width, height);
}
