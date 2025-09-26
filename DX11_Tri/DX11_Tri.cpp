// DX11_Tri_Texture.cpp
// 텍스처 매핑 삼각형 예제
#include <windows.h>
#include <wrl/client.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <vector>
#include <wincodec.h>
#include <memory>
#include <commdlg.h>
#include "Resource.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "D3DCompiler.lib")
#pragma comment(lib, "dxguid.lib")

using Microsoft::WRL::ComPtr;
using namespace DirectX;

HWND g_hWnd = nullptr;
UINT g_ClientWidth = 1600, g_ClientHeight = 900;

ComPtr<ID3D11Device> g_Device;
ComPtr<ID3D11DeviceContext> g_Context;
ComPtr<IDXGISwapChain> g_SwapChain;
ComPtr<ID3D11RenderTargetView> g_RTV;

ComPtr<ID3D11VertexShader> g_VS;
ComPtr<ID3D11PixelShader>  g_PS;
ComPtr<ID3D11InputLayout>  g_Layout;
ComPtr<ID3D11Buffer>       g_VB;
ComPtr<ID3D11Buffer>       g_cbTransform;

// 텍스처 & 샘플러
ComPtr<ID3D11ShaderResourceView> g_TextureSRV;
ComPtr<ID3D11SamplerState>       g_Sampler;

// 이미지 크기 비율 및 기울기 제어
float g_ImageAspectRatio = 1.0f; // 이미지 가로/세로 비율
float g_TiltAngleX = 0.0f;       // X축 기울기 각도 (라디안)
float g_TiltAngleY = 0.0f;       // Y축 기울기 각도 (라디안)
float g_ZoomScale = 1.0f;        // 확대/축소 배율
bool g_IsMouseDown = false;      // 마우스 클릭 상태

// 배경색 설정
float g_BackgroundColor[4] = { 0.f, 0.f, 0.f, 1.0f };

struct Vertex {
    XMFLOAT3 pos;
    XMFLOAT2 uv;
};

// PNG 파일 로딩 함수
HRESULT LoadTextureFromPNG(ID3D11Device* device, const wchar_t* filename, ID3D11ShaderResourceView** srv) {
    // WIC 팩토리 생성
    ComPtr<IWICImagingFactory> wicFactory;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wicFactory));
    if (FAILED(hr)) return hr;

    // 파일 디코더 생성
    ComPtr<IWICBitmapDecoder> decoder;
    hr = wicFactory->CreateDecoderFromFilename(filename, nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder);
    if (FAILED(hr)) return hr;

    // 첫 번째 프레임 가져오기
    ComPtr<IWICBitmapFrameDecode> frame;
    hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr)) return hr;

    // 포맷 변환기 생성
    ComPtr<IWICFormatConverter> converter;
    hr = wicFactory->CreateFormatConverter(&converter);
    if (FAILED(hr)) return hr;

    // RGBA 포맷으로 변환
    hr = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) return hr;

    // 이미지 크기 가져오기
    UINT width, height;
    hr = converter->GetSize(&width, &height);
    if (FAILED(hr)) return hr;
    
    // 이미지 비율 저장
    g_ImageAspectRatio = (float)width / (float)height;

    // 픽셀 데이터 읽기
    std::vector<BYTE> pixels(width * height * 4);
    hr = converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(pixels.size()), pixels.data());
    if (FAILED(hr)) return hr;

    // D3D11 텍스처 생성
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = pixels.data();
    initData.SysMemPitch = width * 4;

    ComPtr<ID3D11Texture2D> texture;
    hr = device->CreateTexture2D(&desc, &initData, &texture);
    if (FAILED(hr)) return hr;

    // Shader Resource View 생성
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = desc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;

    return device->CreateShaderResourceView(texture.Get(), &srvDesc, srv);
}

// 메뉴 생성 함수
void CreateMenuBar(HWND hWnd) {
    HMENU hMenu = CreateMenu();
    HMENU hFileMenu = CreatePopupMenu();
    HMENU hViewMenu = CreatePopupMenu();
    HMENU hBackgroundMenu = CreatePopupMenu();
    
    // 파일 메뉴
    AppendMenu(hFileMenu, MF_STRING, IDM_OPEN_IMAGE, L"이미지 열기...");
    AppendMenu(hFileMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenu(hFileMenu, MF_STRING, IDM_EXIT, L"종료");
    
    // 보기 메뉴
    AppendMenu(hViewMenu, MF_STRING, IDM_RESET_VIEW, L"뷰 리셋");
    
    // 배경색 메뉴
    AppendMenu(hBackgroundMenu, MF_STRING, IDM_BG_COLOR, L"색상 선택...");
    AppendMenu(hBackgroundMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenu(hBackgroundMenu, MF_STRING, IDM_BG_BLACK, L"검은색");
    AppendMenu(hBackgroundMenu, MF_STRING, IDM_BG_WHITE, L"흰색");
    AppendMenu(hBackgroundMenu, MF_STRING, IDM_BG_GRAY, L"회색");
    AppendMenu(hBackgroundMenu, MF_STRING, IDM_BG_BLUE, L"파란색");
    AppendMenu(hBackgroundMenu, MF_STRING, IDM_BG_GREEN, L"초록색");
    AppendMenu(hBackgroundMenu, MF_STRING, IDM_BG_RED, L"빨간색");
    
    // 메인 메뉴
    AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hFileMenu, L"파일(&F)");
    AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hViewMenu, L"보기(&V)");
    AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hBackgroundMenu, L"배경(&B)");
    
    SetMenu(hWnd, hMenu);
}

// 파일 다이얼로그로 이미지 로드
HRESULT LoadImageFromDialog() {
    OPENFILENAME ofn = {};
    wchar_t szFile[260] = {};
    
    ofn.lStructSize = sizeof(OPENFILENAME);
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile) / sizeof(wchar_t);
    ofn.lpstrFilter = L"이미지 파일\0*.png;*.jpg;*.jpeg;*.bmp;*.gif\0PNG 파일\0*.png\0JPEG 파일\0*.jpg;*.jpeg\0BMP 파일\0*.bmp\0모든 파일\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrTitle = L"이미지 파일 선택";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
    
    if (GetOpenFileName(&ofn)) {
        // 기존 텍스처 해제
        g_TextureSRV.Reset();
        
        // 새 이미지 로드
        if (SUCCEEDED(LoadTextureFromPNG(g_Device.Get(), szFile, g_TextureSRV.GetAddressOf()))) {
            // 뷰 리셋
            g_TiltAngleX = 0.0f;
            g_TiltAngleY = 0.0f;
            g_ZoomScale = 1.0f;
            return S_OK;
        }
    }
    return E_FAIL;
}

// 색상 선택 다이얼로그
void ShowColorDialog() {
    CHOOSECOLOR cc = {};
    static COLORREF acrCustClr[16] = {};
    
    cc.lStructSize = sizeof(CHOOSECOLOR);
    cc.lpCustColors = acrCustClr;
    cc.Flags = CC_FULLOPEN | CC_RGBINIT;
    
    // 현재 배경색을 RGB로 변환
    COLORREF currentColor = RGB(
        (int)(g_BackgroundColor[0] * 255),
        (int)(g_BackgroundColor[1] * 255),
        (int)(g_BackgroundColor[2] * 255)
    );
    cc.rgbResult = currentColor;
    
    if (ChooseColor(&cc)) {
        // 선택된 색상을 float 배열로 변환
        g_BackgroundColor[0] = GetRValue(cc.rgbResult) / 255.0f;
        g_BackgroundColor[1] = GetGValue(cc.rgbResult) / 255.0f;
        g_BackgroundColor[2] = GetBValue(cc.rgbResult) / 255.0f;
        g_BackgroundColor[3] = 1.0f; // 알파는 항상 1.0
    }
}

// 동적 정점 업데이트 함수
void UpdateVertices() {
    // 이미지 비율에 맞는 사각형 크기 계산 (정확한 비율 적용)
    float baseSize = 0.6f; // 기본 크기
    
    float width, height;
    if (g_ImageAspectRatio > 1.0f) {
        // 가로가 더 긴 이미지 (landscape)
        width = baseSize;
        height = baseSize / g_ImageAspectRatio;
    } else {
        // 세로가 더 긴 이미지 (portrait)
        width = baseSize * g_ImageAspectRatio;
        height = baseSize;
    }
    
    // 확대/축소 적용
    width *= g_ZoomScale;
    height *= g_ZoomScale;
    
    // 3D 공간에서의 기울기 적용 (X축과 Y축 회전)
    float cosX = cosf(g_TiltAngleX);
    float sinX = sinf(g_TiltAngleX);
    float cosY = cosf(g_TiltAngleY);
    float sinY = sinf(g_TiltAngleY);
    
    // 3D 변환을 위한 정점 생성
    XMFLOAT3 corners[4] = {
        { -width/2,  height/2, 0.0f }, // 왼쪽 위
        {  width/2,  height/2, 0.0f }, // 오른쪽 위
        { -width/2, -height/2, 0.0f }, // 왼쪽 아래
        {  width/2, -height/2, 0.0f }  // 오른쪽 아래
    };
    
    // X축과 Y축 회전으로 3D 기울기 적용
    for (int i = 0; i < 4; i++) {
        float x = corners[i].x;
        float y = corners[i].y;
        float z = corners[i].z;
        
        // Y축 회전 (좌우 기울기)
        float newX = x * cosY - z * sinY;
        float newZ = x * sinY + z * cosY;
        
        // X축 회전 (상하 기울기)
        corners[i].x = newX;
        corners[i].y = y * cosX - newZ * sinX;
        corners[i].z = y * sinX + newZ * cosX;
    }
    
    // 정점 데이터 생성 (2개 삼각형으로 사각형 구성)
    Vertex vertices[] = {
        // 위쪽 삼각형
        { corners[0], XMFLOAT2(0.0f, 0.0f) }, // 왼쪽 위
        { corners[1], XMFLOAT2(1.0f, 0.0f) }, // 오른쪽 위
        { corners[2], XMFLOAT2(0.0f, 1.0f) }, // 왼쪽 아래
        
        // 아래쪽 삼각형
        { corners[1], XMFLOAT2(1.0f, 0.0f) }, // 오른쪽 위
        { corners[3], XMFLOAT2(1.0f, 1.0f) }, // 오른쪽 아래
        { corners[2], XMFLOAT2(0.0f, 1.0f) }, // 왼쪽 아래
    };
    
    // 정점 버퍼 업데이트
    D3D11_MAPPED_SUBRESOURCE mapped;
    g_Context->Map(g_VB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    memcpy(mapped.pData, vertices, sizeof(vertices));
    g_Context->Unmap(g_VB.Get(), 0);
}

HRESULT CompileShader(LPCWSTR file, LPCSTR entry, LPCSTR target, ID3DBlob** blob) {
    *blob = nullptr;
    ComPtr<ID3DBlob> error;
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
    HRESULT hr = D3DCompileFromFile(file, nullptr, nullptr, entry, target, flags, 0, blob, error.GetAddressOf());
    if (FAILED(hr)) {
        if (error) MessageBoxA(nullptr, (char*)error->GetBufferPointer(), "Shader Error", MB_OK);
    }
    return hr;
}

HRESULT CreateRenderTargetView() {
    g_RTV.Reset();
    ComPtr<ID3D11Texture2D> backBuffer;
    HRESULT hr = g_SwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)backBuffer.GetAddressOf());
    if (FAILED(hr)) return hr;
    hr = g_Device->CreateRenderTargetView(backBuffer.Get(), nullptr, g_RTV.GetAddressOf());
    if (FAILED(hr)) return hr;
    g_Context->OMSetRenderTargets(1, g_RTV.GetAddressOf(), nullptr);

    D3D11_VIEWPORT vp = { 0.0f,0.0f,(FLOAT)g_ClientWidth,(FLOAT)g_ClientHeight,0.0f,1.0f };
    g_Context->RSSetViewports(1, &vp);
    return S_OK;
}

HRESULT InitD3D(HWND hWnd) {
    g_hWnd = hWnd;
    RECT rc; GetClientRect(hWnd, &rc);
    g_ClientWidth = rc.right - rc.left;
    g_ClientHeight = rc.bottom - rc.top;

    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 1;
    sd.BufferDesc.Width = g_ClientWidth;
    sd.BufferDesc.Height = g_ClientHeight;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;

    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        nullptr, 0, D3D11_SDK_VERSION, &sd, g_SwapChain.GetAddressOf(),
        g_Device.GetAddressOf(), nullptr, g_Context.GetAddressOf());
    if (FAILED(hr)) return hr;

    return CreateRenderTargetView();
}

HRESULT CreateResources() {
    // --- 셰이더 ---
    ComPtr<ID3DBlob> vsBlob, psBlob;
    if (FAILED(CompileShader(L"shader.hlsl", "VSMain", "vs_5_0", vsBlob.GetAddressOf()))) return E_FAIL;
    if (FAILED(CompileShader(L"shader.hlsl", "PSMain", "ps_5_0", psBlob.GetAddressOf()))) return E_FAIL;

    g_Device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, g_VS.GetAddressOf());
    g_Device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, g_PS.GetAddressOf());

    // --- 입력 레이아웃 ---
    D3D11_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0 },
        { "TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0 }
    };
    g_Device->CreateInputLayout(layout, 2, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), g_Layout.GetAddressOf());

    // --- 정점 버퍼 (사각형) ---
    Vertex vertices[] = {
        // 위쪽 삼각형
        { XMFLOAT3(-0.5f,  0.5f, 0.0f), XMFLOAT2(0.0f, 0.0f) }, // 왼쪽 위
        { XMFLOAT3( 0.5f,  0.5f, 0.0f), XMFLOAT2(1.0f, 0.0f) }, // 오른쪽 위
        { XMFLOAT3(-0.5f, -0.5f, 0.0f), XMFLOAT2(0.0f, 1.0f) }, // 왼쪽 아래
        
        // 아래쪽 삼각형
        { XMFLOAT3( 0.5f,  0.5f, 0.0f), XMFLOAT2(1.0f, 0.0f) }, // 오른쪽 위
        { XMFLOAT3( 0.5f, -0.5f, 0.0f), XMFLOAT2(1.0f, 1.0f) }, // 오른쪽 아래
        { XMFLOAT3(-0.5f, -0.5f, 0.0f), XMFLOAT2(0.0f, 1.0f) }, // 왼쪽 아래
    };
    // 동적 업데이트를 위해 D3D11_USAGE_DYNAMIC 사용
    D3D11_BUFFER_DESC bd = { sizeof(vertices), D3D11_USAGE_DYNAMIC, D3D11_BIND_VERTEX_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0 };
    D3D11_SUBRESOURCE_DATA init = { vertices };
    g_Device->CreateBuffer(&bd, &init, g_VB.GetAddressOf());

    // --- 상수 버퍼 ---
    D3D11_BUFFER_DESC cbd = { sizeof(XMFLOAT4X4), D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER,0,0,0 };
    g_Device->CreateBuffer(&cbd, nullptr, g_cbTransform.GetAddressOf());

    // --- PNG 텍스처 로드 ---
    if (FAILED(LoadTextureFromPNG(g_Device.Get(), L"texture.png", g_TextureSRV.GetAddressOf()))) {
        OutputDebugStringA("PNG 텍스처 로드 실패. texture.png 파일을 확인하세요.\n");
        return E_FAIL;
    }

    // --- 샘플러 ---
    D3D11_SAMPLER_DESC samp = {};
    samp.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samp.AddressU = samp.AddressV = samp.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    g_Device->CreateSamplerState(&samp, g_Sampler.GetAddressOf());

    return S_OK;
}

void RenderFrame() {
    // 동적 정점 업데이트
    UpdateVertices();
    
    // 3D 변환 행렬 설정
    XMMATRIX world = XMMatrixIdentity();
    
    // 카메라 뷰 (약간 뒤에서 바라보기)
    XMMATRIX view = XMMatrixLookAtLH(
        XMVectorSet(0.0f, 0.0f, -2.0f, 0.0f), // 카메라 위치
        XMVectorSet(0.0f, 0.0f, 0.0f, 0.0f),   // 바라보는 지점
        XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f)    // 업 벡터
    );
    
    // 원근 투영
    float fov = XM_PI / 4.0f; // 45도 시야각
    float aspect = (float)g_ClientWidth / (float)g_ClientHeight;
    XMMATRIX proj = XMMatrixPerspectiveFovLH(fov, aspect, 0.1f, 100.0f);
    
    XMMATRIX wvp = world * view * proj;
    XMFLOAT4X4 mat; XMStoreFloat4x4(&mat, XMMatrixTranspose(wvp));
    g_Context->UpdateSubresource(g_cbTransform.Get(), 0, nullptr, &mat, 0, 0);
    g_Context->VSSetConstantBuffers(0, 1, g_cbTransform.GetAddressOf());

    g_Context->ClearRenderTargetView(g_RTV.Get(), g_BackgroundColor);

    UINT stride = sizeof(Vertex), offset = 0;
    g_Context->IASetInputLayout(g_Layout.Get());
    g_Context->IASetVertexBuffers(0, 1, g_VB.GetAddressOf(), &stride, &offset);
    g_Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    g_Context->VSSetShader(g_VS.Get(), nullptr, 0);
    g_Context->PSSetShader(g_PS.Get(), nullptr, 0);

    g_Context->PSSetShaderResources(0, 1, g_TextureSRV.GetAddressOf());
    g_Context->PSSetSamplers(0, 1, g_Sampler.GetAddressOf());

    g_Context->Draw(6, 0); // 사각형은 6개 정점 (2개 삼각형)
    g_SwapChain->Present(1, 0);
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED) {
            g_ClientWidth = LOWORD(lParam);
            g_ClientHeight = HIWORD(lParam);
            if (g_Context) {
                g_Context->OMSetRenderTargets(0, nullptr, nullptr);
                g_RTV.Reset();
                g_SwapChain->ResizeBuffers(0, g_ClientWidth, g_ClientHeight, DXGI_FORMAT_UNKNOWN, 0);
                CreateRenderTargetView();
            }
        }
        return 0;
    case WM_LBUTTONDOWN:
        g_IsMouseDown = true;
        SetCapture(hWnd);
        return 0;
    case WM_LBUTTONUP:
        g_IsMouseDown = false;
        ReleaseCapture();
        return 0;
    case WM_MOUSEMOVE:
        if (g_IsMouseDown) {
            // 마우스 X, Y 좌표로 기울기 조절
            int mouseX = LOWORD(lParam);
            int mouseY = HIWORD(lParam);
            float normalizedX = (float)(mouseX - g_ClientWidth / 2) / (g_ClientWidth / 2.0f);
            float normalizedY = (float)(mouseY - g_ClientHeight / 2) / (g_ClientHeight / 2.0f);
            g_TiltAngleY = normalizedX * 1.2f; // 좌우 기울기
            g_TiltAngleX = -normalizedY * 1.2f; // 상하 기울기 (Y축 반전)
        }
        return 0;
    case WM_KEYDOWN:
        switch (wParam) {
        case VK_LEFT:  g_TiltAngleY -= 0.1f; break;  // 왼쪽
        case VK_RIGHT: g_TiltAngleY += 0.1f; break;  // 오른쪽
        case VK_UP:    g_TiltAngleX -= 0.1f; break;  // 위쪽
        case VK_DOWN:  g_TiltAngleX += 0.1f; break;  // 아래쪽
        case VK_SPACE: // 스페이스바로 리셋
            g_TiltAngleX = 0.0f;
            g_TiltAngleY = 0.0f;
            break;
        }
        // 각도 제한
        g_TiltAngleX = max(-1.5f, min(1.5f, g_TiltAngleX));
        g_TiltAngleY = max(-1.5f, min(1.5f, g_TiltAngleY));
        return 0;
    case WM_MOUSEWHEEL:
        {
            // 마우스 스크롤로 확대/축소
            int wheelDelta = GET_WHEEL_DELTA_WPARAM(wParam);
            float zoomFactor = wheelDelta > 0 ? 1.1f : 0.9f;
            g_ZoomScale *= zoomFactor;
            g_ZoomScale = max(0.1f, min(5.0f, g_ZoomScale)); // 0.1배 ~ 5배 제한
        }
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDM_OPEN_IMAGE:
            LoadImageFromDialog();
            return 0;
        case IDM_RESET_VIEW:
            g_TiltAngleX = 0.0f;
            g_TiltAngleY = 0.0f;
            g_ZoomScale = 1.0f;
            return 0;
        case IDM_BG_COLOR:
            ShowColorDialog();
            return 0;
        case IDM_BG_BLACK:
            g_BackgroundColor[0] = 0.0f; g_BackgroundColor[1] = 0.0f; g_BackgroundColor[2] = 0.0f;
            return 0;
        case IDM_BG_WHITE:
            g_BackgroundColor[0] = 1.0f; g_BackgroundColor[1] = 1.0f; g_BackgroundColor[2] = 1.0f;
            return 0;
        case IDM_BG_GRAY:
            g_BackgroundColor[0] = 0.5f; g_BackgroundColor[1] = 0.5f; g_BackgroundColor[2] = 0.5f;
            return 0;
        case IDM_BG_BLUE:
            g_BackgroundColor[0] = 0.0f; g_BackgroundColor[1] = 0.0f; g_BackgroundColor[2] = 1.0f;
            return 0;
        case IDM_BG_GREEN:
            g_BackgroundColor[0] = 0.0f; g_BackgroundColor[1] = 1.0f; g_BackgroundColor[2] = 0.0f;
            return 0;
        case IDM_BG_RED:
            g_BackgroundColor[0] = 1.0f; g_BackgroundColor[1] = 0.0f; g_BackgroundColor[2] = 0.0f;
            return 0;
        case IDM_EXIT:
            PostQuitMessage(0);
            return 0;
        }
        return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    default: return DefWindowProc(hWnd, msg, wParam, lParam);
    }
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int nCmdShow) {
    // COM 초기화 (WIC 사용을 위해 필요)
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    
    WNDCLASS wc = {}; wc.lpfnWndProc = WndProc; wc.hInstance = hInst; wc.lpszClassName = L"DX11TexWin";
    RegisterClass(&wc);
    RECT rc = { 0,0,(LONG)g_ClientWidth,(LONG)g_ClientHeight }; AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hWnd = CreateWindow(wc.lpszClassName, L"DX11 3D Image Viewer", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top,
        nullptr, nullptr, hInst, nullptr);

    // 메뉴바 생성
    CreateMenuBar(hWnd);

    ShowWindow(hWnd, nCmdShow);

    if (FAILED(InitD3D(hWnd))) return -1;
    if (FAILED(CreateResources())) return -1;

    MSG msg = {};
    while (msg.message != WM_QUIT) {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessage(&msg); }
        else RenderFrame();
    }
    return (int)msg.wParam;
}
