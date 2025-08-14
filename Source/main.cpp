#include "app_state.h"
#include "graphics.h"
#include "resource.h" 
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include "ui.h"
#include <tchar.h>

// ȫ�ֱ�������
AppState g_app_state;
std::thread g_worker_thread;
std::thread g_cdn_worker_thread;
std::atomic<bool> g_stop_flag(false);
bool g_overall_success = false;
ID3D11Device* g_pd3dDevice = nullptr;
ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
IDXGISwapChain* g_pSwapChain = nullptr;
ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;

// Win32 ���ڹ��̺���
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// ������
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PSTR lpCmdLine, int nCmdShow) {
    // 1. ����Ӧ�ô���
    WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(NULL), NULL, NULL, NULL, NULL, _T("ImGui Weibo Image Restorer"), NULL };
    wc.hIcon = (HICON)LoadImage(hInstance, MAKEINTRESOURCE(IDI_ICON1), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE | LR_SHARED);
    wc.hIconSm = (HICON)LoadImage(hInstance, MAKEINTRESOURCE(IDI_ICON1), IMAGE_ICON, 16, 16, LR_SHARED);
    ::RegisterClassEx(&wc);
    HWND hwnd = ::CreateWindow(wc.lpszClassName, L"��ȥ����", WS_OVERLAPPEDWINDOW, 500, 300, 590, 600, NULL, NULL, wc.hInstance, NULL);

    // 2. ��ʼ�� Direct3D
    if (!CreateDeviceD3D(hwnd)) {
        CleanupDeviceD3D();
        ::UnregisterClass(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    // 3. ��ʾ����
    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);

    // 4. ��ʼ�� ImGui
    InitUI(hwnd);

    // 5. ����Ĭ��ռλͼƬ
    unsigned char placeholder_pixels[4] = { 80, 80, 80, 255 };
    LoadTextureFromMemory(placeholder_pixels, 4, &g_app_state.image_texture, &g_app_state.image_width, &g_app_state.image_height);
    g_app_state.image_width = 1; g_app_state.image_height = 1;

    // 6. ��ѭ��
    bool done = false;
    while (!done) {
        MSG msg;
        while (::PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE)) {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
                done = true;
        }
        if (done) break;

        // �ȴ������߳̽���
        if (!g_app_state.is_running && g_worker_thread.joinable()) {
            g_worker_thread.join();
        }
        // �������ȴ�CDN�߳̽���
        if (!g_app_state.is_cdn_running && g_cdn_worker_thread.joinable()) {
            g_cdn_worker_thread.join();
        }

        // ��ȾUI
        RenderUI();

        // ִ����Ⱦ
        const float clear_color_with_alpha[4] = { 0.45f, 0.55f, 0.60f, 1.00f };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, NULL);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color_with_alpha);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_pSwapChain->Present(1, 0);
    }

    // 7. ����
    if (g_worker_thread.joinable()) {
        g_stop_flag = true;
        g_worker_thread.join();
    }
    // ����������CDN�߳�
    if (g_cdn_worker_thread.joinable()) {
        g_cdn_worker_thread.join();
    }


    if (g_app_state.image_texture) g_app_state.image_texture->Release();

    ShutdownUI();
    CleanupDeviceD3D();
    ::DestroyWindow(hwnd);
    ::UnregisterClass(wc.lpszClassName, wc.hInstance);
    return 0;
}


// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg) {
    case WM_SIZE:
        if (g_pd3dDevice != NULL && wParam != SIZE_MINIMIZED) {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU)
            return 0;
        break;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProc(hWnd, msg, wParam, lParam);
}