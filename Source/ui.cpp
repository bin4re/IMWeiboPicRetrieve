#include "ui.h"
#include "app_state.h"
#include "worker.h"
#include "utils.h"
#include "graphics.h"
#include "resource.h" 

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

#include <fstream>


void InitUI(HWND hwnd) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;

    ImFont* font = io.Fonts->AddFontFromFileTTF("C://Windows//Fonts//msyh.ttc", 16.5f, nullptr, io.Fonts->GetGlyphRangesChineseFull());
    io.FontDefault = font;

    ImGui::StyleColorsDark();
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);
}

void ShutdownUI() {
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
}

void RenderUI() {
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(io.DisplaySize, ImGuiCond_Always);
    ImGui::Begin("Main", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

    // 图片显示区
    float window_width = ImGui::GetWindowSize().x;
    float image_box_width = 450.0f;
    float image_box_height = 300.0f;
    float box_cursor_x = (window_width - image_box_width) * 0.5f;
    ImGui::SetCursorPosX(box_cursor_x);
    ImGui::GetWindowDrawList()->AddRectFilled(
        ImGui::GetCursorScreenPos(),
        ImVec2(ImGui::GetCursorScreenPos().x + image_box_width, ImGui::GetCursorScreenPos().y + image_box_height),
        IM_COL32(30, 30, 30, 255));
    ImVec2 final_image_size = ImVec2(0, 0);
    if (g_app_state.image_width > 0 && g_app_state.image_height > 0) {
        float aspect_ratio = (float)g_app_state.image_width / (float)g_app_state.image_height;
        final_image_size.x = image_box_width;
        final_image_size.y = image_box_width / aspect_ratio;
        if (final_image_size.y > image_box_height) {
            final_image_size.y = image_box_height;
            final_image_size.x = image_box_height * aspect_ratio;
        }
    }
    float image_cursor_x = box_cursor_x + (image_box_width - final_image_size.x) * 0.5f;
    float image_cursor_y = ImGui::GetCursorPosY() + (image_box_height - final_image_size.y) * 0.5f;
    ImGui::SetCursorPos(ImVec2(image_cursor_x, image_cursor_y));
    ImGui::Image((void*)g_app_state.image_texture, final_image_size);
    ImGui::SetCursorPos(ImVec2(0, image_box_height + 10));


    // 按钮
    bool start_pressed = false;
    bool cdn_pressed = false; // 新增
    float button_width = 85.0f; // 调整按钮宽度以适应新按钮
    float button_spacing = 10.0f;
    float total_buttons_width = 5 * button_width + 4 * button_spacing; // 修改为5个按钮
    float buttons_start_x = (window_width - total_buttons_width) * 0.5f;
    ImGui::SetCursorPosX(buttons_start_x);

    // 当CDN或主任务运行时，禁用开始按钮
    if (g_app_state.is_running || g_app_state.is_cdn_running) ImGui::BeginDisabled();
    if (ImGui::Button(U8("开始"), ImVec2(button_width, 0))) {
        start_pressed = true;
    }
    if (g_app_state.is_running || g_app_state.is_cdn_running) ImGui::EndDisabled();

    ImGui::SameLine(0, button_spacing);

    const bool stop_button_disabled = !g_app_state.is_running || g_stop_flag || g_overall_success;
    if (stop_button_disabled) ImGui::BeginDisabled();
    if (ImGui::Button(U8("停止"), ImVec2(button_width, 0))) {
        g_stop_flag = true;
    }
    if (stop_button_disabled) ImGui::EndDisabled();

    ImGui::SameLine(0, button_spacing);
    const bool save_button_disabled = !g_app_state.save_button_enabled;
    if (save_button_disabled) ImGui::BeginDisabled();
    if (ImGui::Button(U8("保存"), ImVec2(button_width, 0))) {
        std::string save_path = ShowSaveFileDialog(g_app_state.current_filename.c_str());
        if (!save_path.empty()) {
            std::ofstream file(save_path, std::ios::binary);
            if (file.is_open()) {
                std::lock_guard<std::mutex> lock(g_app_state.mtx);
                file.write(reinterpret_cast<const char*>(g_app_state.downloaded_image_data.data()), g_app_state.downloaded_image_data.size());
                g_app_state.log_output += U8("图片已保存至: ") + save_path + U8("。\n");
            }
            else {
                std::lock_guard<std::mutex> lock(g_app_state.mtx);
                g_app_state.log_output += U8("错误: 无法打开文件进行写入。\n");
            }
        }
    }
    if (save_button_disabled) ImGui::EndDisabled();


    ImGui::SameLine(0, button_spacing);
    const bool clear_button_disabled = !g_app_state.clear_button_enabled || g_app_state.is_running || g_app_state.is_cdn_running;
    if (clear_button_disabled) ImGui::BeginDisabled();
    if (ImGui::Button(U8("清除"), ImVec2(button_width, 0))) {
        if (g_app_state.image_texture) {
            g_app_state.image_texture->Release();
            g_app_state.image_texture = nullptr;
        }
        unsigned char placeholder_pixels[4] = { 80, 80, 80, 255 };
        int temp_w, temp_h;
        LoadTextureFromMemory(placeholder_pixels, 4, &g_app_state.image_texture, &temp_w, &temp_h);
        g_app_state.image_width = 1;
        g_app_state.image_height = 1;
        g_app_state.save_button_enabled = false;
        g_app_state.clear_button_enabled = false;
        {
            std::lock_guard<std::mutex> lock(g_app_state.mtx);
            g_app_state.downloaded_image_data.clear();
            g_app_state.current_filename.clear();
            g_app_state.log_output += U8("图片已清理。\n");
        }
    }
    if (clear_button_disabled) ImGui::EndDisabled();

    // CDN按钮
    ImGui::SameLine(0, button_spacing);
    if (g_app_state.is_running || g_app_state.is_cdn_running) ImGui::BeginDisabled();
    if (ImGui::Button(U8("CDN"), ImVec2(button_width, 0))) {
        cdn_pressed = true;
    }
    if (g_app_state.is_running || g_app_state.is_cdn_running) ImGui::EndDisabled();


    // URL输入框
    ImGui::PushItemWidth(500);
    ImGui::InputTextWithHint("URL", U8("粘贴 url 到此处..."), g_app_state.url_buffer, sizeof(g_app_state.url_buffer));
    ImGui::PopItemWidth();


    // 进度条和进度文本
    int current_progress = g_app_state.total_progress_counter.load();
    int total_progress = g_app_state.ip_progress_total;
    float progress_fraction = (total_progress > 0) ? static_cast<float>(current_progress) / total_progress : 0.0f;

    ImGui::PushItemWidth(500);
    ImGui::ProgressBar(progress_fraction, ImVec2(0.0f, 0.0f));
    ImGui::PopItemWidth();
    ImGui::SameLine();
    ImGui::Text("%d/%d", current_progress, total_progress);

    // 信息显示框
    {
        std::lock_guard<std::mutex> lock(g_app_state.mtx);
        ImGui::InputTextMultiline("##Log", &g_app_state.log_output[0], g_app_state.log_output.capacity(), ImVec2(-1, ImGui::GetContentRegionAvail().y), ImGuiInputTextFlags_ReadOnly);
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
            ImGui::SetScrollHereY(1.0f);
    }

    ImGui::End();

    // 处理开始按钮点击事件
    if (start_pressed) {
        StartDownloadWorkerThread(std::string(g_app_state.url_buffer));
    }

    // 处理CDN按钮点击事件
    if (cdn_pressed) {
        StartCDNWorkerThread();
    }


    // 渲染
    ImGui::Render();
}