#pragma once

#include <d3d11.h>
#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <thread>
#if defined(__cpp_char8_t)
#define U8(str) reinterpret_cast<const char*>(u8##str)
#else
#define U8(str) u8##str
#endif
// 全局状态和数据
//-----------------------------------------------------------------------------
struct AppState {
    // --- UI State ---
    char url_buffer[1024] = { 0 };
    std::string log_output = U8("提示:\n1. 图片时间越近，浏览人数越多，成功恢复几率越高。\n2. 初始请求或点击 CDN 会获取微博图床海外 CDN IP，验证更新到 ips.txt 文件中。\n3. 在浏览器中右击目标图片，选择“复制图片链接”并粘贴到输入框。\n");
    float overall_progress = 0.0f;
    std::atomic<int> total_progress_counter = { 0 };
    int ip_progress_total = 0;
    bool save_button_enabled = false;
    bool clear_button_enabled = false;
    std::atomic<bool> is_running = { false };
    std::atomic<bool> is_cdn_running = { false };
    // --- Image Data ---
    ID3D11ShaderResourceView* image_texture = nullptr;
    int image_width = 0;
    int image_height = 0;
    std::vector<unsigned char> downloaded_image_data;
    std::string current_filename;

    // --- Thread Synchronization ---
    std::mutex mtx; // 用于保护非原子数据（如log, downloaded_image_data, image_texture）
};

// 全局实例
extern AppState g_app_state;
extern std::thread g_worker_thread;
extern std::thread g_cdn_worker_thread;
extern std::atomic<bool> g_stop_flag;
extern bool g_overall_success;

// DirectX 全局变量
extern ID3D11Device* g_pd3dDevice;
extern ID3D11DeviceContext* g_pd3dDeviceContext;
extern IDXGISwapChain* g_pSwapChain;
extern ID3D11RenderTargetView* g_mainRenderTargetView;