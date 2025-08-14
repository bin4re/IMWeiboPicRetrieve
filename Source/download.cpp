#include "worker.h"

// 用于关联CURL句柄和请求数据
struct TransferData {
        HttpResponse response;
        std::string ip;
        std::string scale;
        std::string filename;
        CURL* easy_handle = nullptr;
};

// URL解析结构体和函数
struct ParsedUrl {
        std::string domain;
        std::string scale;
        std::string filename;
        bool is_valid = false;
};

ParsedUrl ParseWeiboUrl(const std::string& url_str) {
        ParsedUrl result;
        size_t protocol_pos = url_str.find("://");
        if (protocol_pos == std::string::npos) return result;
        size_t path_start_pos = url_str.find('/', protocol_pos + 3);
        if (path_start_pos == std::string::npos) return result;
        result.domain = url_str.substr(protocol_pos + 3, path_start_pos - (protocol_pos + 3));
        size_t scale_end_pos = url_str.find('/', path_start_pos + 1);
        if (scale_end_pos == std::string::npos) return result;
        result.scale = url_str.substr(path_start_pos + 1, scale_end_pos - (path_start_pos + 1));
        result.filename = url_str.substr(scale_end_pos + 1);
        if (!result.domain.empty() && !result.scale.empty() && !result.filename.empty()) {
                result.is_valid = true;
        }
        return result;
}

// StartWorkerThread 函数
void StartDownloadWorkerThread(const std::string& url) {
        if (g_worker_thread.joinable()) {
                g_worker_thread.join();
        }
        g_app_state.log_output.clear();
        g_app_state.log_output.reserve(16384);
        g_app_state.total_progress_counter = 0;
        g_app_state.ip_progress_total = 0;
        g_app_state.save_button_enabled = false;
        g_app_state.is_running = true;
        g_stop_flag = false;
        g_overall_success = false;
        g_worker_thread = std::thread(DownloadWorkerThreadFunction, url);
}


// 工作线程
void DownloadWorkerThreadFunction(std::string url) {
        //  解析URL
        ParsedUrl parsed_url = ParseWeiboUrl(url);
        if (!parsed_url.is_valid) {
                std::lock_guard<std::mutex> lock(g_app_state.mtx);
                g_app_state.log_output += U8("错误:URL 未输入或无法解析。\n");
                g_app_state.is_running = false;
                return;
        }

        std::call_once(curl_init_flag, InitCurlGlobal);

        // 检查 ips.txt 是否存在，如果不存在则自动运行CDN功能 ---
        std::ifstream ip_file_check("ips.txt");
        if (!ip_file_check.is_open()) {
                {
                        std::lock_guard<std::mutex> lock(g_app_state.mtx);
                        g_app_state.log_output += U8("提示: ips.txt 文件不存在，将自动执行 CDN IP 获取。\n----------------------------------------\n");
                }
                bool cdn_success = RunGetCDN_Sync();
                if (!cdn_success) {
                        std::lock_guard<std::mutex> lock(g_app_state.mtx);
                        g_app_state.log_output += U8("CDN IP 获取失败，无法继续。请检查网络或手动创建 ips.txt。\n");
                        g_app_state.is_running = false;
                        return;
                }
                {
                        std::lock_guard<std::mutex> lock(g_app_state.mtx);
                }
        }
        ip_file_check.close();

        // 读取IP列表
        std::vector<std::string> ip_list;
        {
                std::lock_guard<std::mutex> lock(g_app_state.mtx);
                g_app_state.log_output += U8("正在从 ips.txt 读取 IP 列表 ...\n");
        }
        std::ifstream ip_file("ips.txt");
        if (!ip_file.is_open()) {
                std::lock_guard<std::mutex> lock(g_app_state.mtx);
                g_app_state.log_output += U8("错误: 打开 ips.txt 失败。\n");
                g_app_state.is_running = false;
                return;
        }
        std::string line;
        while (std::getline(ip_file, line)) {
                auto first = line.find_first_not_of(" \t\n\r");
                if (first == std::string::npos) continue;
                auto last = line.find_last_not_of(" \t\n\r");
                line = line.substr(first, (last - first + 1));
                if (!line.empty() && line[0] != '#') ip_list.push_back(line);
        }
        ip_file.close();
        if (ip_list.empty()) {
                std::lock_guard<std::mutex> lock(g_app_state.mtx);
                g_app_state.log_output += U8("错误: ips.txt 文件中未找到有效的IP地址。\n");
                g_app_state.is_running = false;
                return;
        }



        // 准备尺寸列表
        std::vector<std::string> scales = { "orj1080", "orj960", "orj360", "large", "woriginal", "mw2000", "mw690", "wap800", "wap720", "bmiddle", "wap360", "thumb300", "wap240", "orj180", "thumbnail", "thumb180", "thumb150", "square" };
        auto it = std::find(scales.begin(), scales.end(), parsed_url.scale);
        if (it != scales.end()) scales.erase(it);
        scales.insert(scales.begin(), parsed_url.scale);

        g_app_state.ip_progress_total = (int)(scales.size() * ip_list.size());
        {
                std::lock_guard<std::mutex> lock(g_app_state.mtx);
                g_app_state.log_output += U8("已找到 ") + std::to_string(ip_list.size()) + U8(" 个 IP，总计尝试请求数: ") + std::to_string(g_app_state.ip_progress_total) + U8("。\n");
                g_app_state.log_output += U8("----------------------------------------\n");
        }


        // 使用 Multi Interface 开始请求循环
        for (const auto& scale : scales) {
                if (g_stop_flag || g_overall_success) break;

                {
                        std::lock_guard<std::mutex> lock(g_app_state.mtx);
                        g_app_state.log_output += U8("正在使用 ") + std::to_string(ip_list.size()) + U8(" 个 IP 并发请求尺寸 ") + scale + U8("。\n");
                }

                CURLM* multi_handle = curl_multi_init();
                if (!multi_handle) continue;

                std::vector<std::unique_ptr<TransferData>> transfers;
                std::vector<curl_slist*> headers_list;

                for (const auto& ip : ip_list) {
                        if (g_stop_flag) break;

                        auto transfer_data = std::make_unique<TransferData>();
                        transfer_data->ip = ip;
                        transfer_data->scale = scale;
                        transfer_data->filename = parsed_url.filename;
                        transfer_data->easy_handle = curl_easy_init();

                        if (!transfer_data->easy_handle) continue;

                        std::string path = "/" + scale + "/" + parsed_url.filename;
                        std::string full_url = "http://" + ip + path;

                        CURL* eh = transfer_data->easy_handle;
                        curl_easy_setopt(eh, CURLOPT_URL, full_url.c_str());
                        curl_easy_setopt(eh, CURLOPT_WRITEFUNCTION, WriteCallback);
                        using ImageHttpResponse = struct { std::vector<uint8_t> data; long response_code; std::string content_type; };
                        ImageHttpResponse image_response; // 临时变量
                        curl_easy_setopt(eh, CURLOPT_WRITEDATA, &transfer_data->response); // 假设 TransferData::response.data 是 vector<char> 或 vector<uint8_t>
                        curl_easy_setopt(eh, CURLOPT_TIMEOUT, 2L);
                        curl_easy_setopt(eh, CURLOPT_CONNECTTIMEOUT, 2L);
                        curl_easy_setopt(eh, CURLOPT_FOLLOWLOCATION, 0L);
                        curl_easy_setopt(eh, CURLOPT_MAXREDIRS, 3L);
                        curl_easy_setopt(eh, CURLOPT_USERAGENT, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36");
                        curl_easy_setopt(eh, CURLOPT_PRIVATE, transfer_data.get());
                        curl_easy_setopt(eh, CURLOPT_SSL_VERIFYPEER, 0L);

                        struct curl_slist* headers = nullptr;
                        std::string host_header = "Host: " + parsed_url.domain;
                        std::string referer_header = "Referer: https://weibo.com";
                        headers = curl_slist_append(headers, host_header.c_str());
                        headers = curl_slist_append(headers, referer_header.c_str());
                        headers = curl_slist_append(headers, "Connection: close");
                        curl_easy_setopt(eh, CURLOPT_HTTPHEADER, headers);
                        headers_list.push_back(headers);

                        curl_multi_add_handle(multi_handle, eh);
                        transfers.push_back(std::move(transfer_data));
                }

                int still_running = 0;
                curl_multi_perform(multi_handle, &still_running);

                do {
                        int numfds;
                        CURLMcode mc = curl_multi_wait(multi_handle, NULL, 0, 1000, &numfds);
                        if (mc != CURLM_OK) break;

                        if (g_stop_flag) break;

                        curl_multi_perform(multi_handle, &still_running);

                        CURLMsg* msg;
                        int msgs_left;
                        while ((msg = curl_multi_info_read(multi_handle, &msgs_left))) {
                                if (msg->msg == CURLMSG_DONE) {
                                        g_app_state.total_progress_counter++;

                                        TransferData* completed_transfer = nullptr;
                                        curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, &completed_transfer);

                                        if (g_stop_flag || g_overall_success) {
                                                // Do nothing
                                        }
                                        else if (msg->data.result == CURLE_OK) {
                                                curl_easy_getinfo(msg->easy_handle, CURLINFO_RESPONSE_CODE, &completed_transfer->response.response_code);
                                                char* ct_ptr = nullptr;
                                                curl_easy_getinfo(msg->easy_handle, CURLINFO_CONTENT_TYPE, &ct_ptr);
                                                if (ct_ptr) completed_transfer->response.content_type = ct_ptr;

                                                if (completed_transfer->response.response_code == 200 &&
                                                        completed_transfer->response.content_type.find("image/") == 0 &&
                                                        !completed_transfer->response.data.empty()) {

                                                        g_overall_success = true;

                                                        std::lock_guard<std::mutex> lock(g_app_state.mtx);
                                                        g_app_state.current_filename = completed_transfer->filename;
                                                        g_app_state.log_output += U8("成功！找到图片，使用 IP: ") + completed_transfer->ip + U8("，尺寸: ") + completed_transfer->scale + U8("。\n");

                                                        // 数据类型转换
                                                        const auto& char_data = completed_transfer->response.data;
                                                        g_app_state.downloaded_image_data.assign(char_data.begin(), char_data.end());

                                                        ID3D11ShaderResourceView* new_texture = nullptr;
                                                        int new_w = 0, new_h = 0;
                                                        if (LoadTextureFromMemory(g_app_state.downloaded_image_data.data(),
                                                                (int)g_app_state.downloaded_image_data.size(),
                                                                &new_texture, &new_w, &new_h)) {
                                                                if (g_app_state.image_texture) g_app_state.image_texture->Release();
                                                                g_app_state.image_texture = new_texture;
                                                                g_app_state.image_width = new_w;
                                                                g_app_state.image_height = new_h;
                                                                g_app_state.save_button_enabled = true;
                                                                g_app_state.clear_button_enabled = true;
                                                                g_app_state.log_output += U8("图片已加载并显示。\n");
                                                        }
                                                        else {
                                                                g_app_state.log_output += U8("错误: 下载的数据无法被解码为图像。\n");
                                                        }
                                                }
                                        }

                                        curl_multi_remove_handle(multi_handle, msg->easy_handle);
                                }
                        }
                        if (g_overall_success || g_stop_flag) {
                                break;
                        }

                } while (still_running > 0);

                // 清理
                for (auto& transfer : transfers) {
                        if (transfer->easy_handle) {
                                curl_multi_remove_handle(multi_handle, transfer->easy_handle);
                                curl_easy_cleanup(transfer->easy_handle);
                                transfer->easy_handle = nullptr;
                        }
                }
                for (auto headers : headers_list) {
                        curl_slist_free_all(headers);
                }
                curl_multi_cleanup(multi_handle);
        }


        // 任务结束日志
        {
                std::lock_guard<std::mutex> lock(g_app_state.mtx);
                g_app_state.log_output += "----------------------------------------\n";
                if (g_stop_flag) {
                        g_app_state.log_output += U8("请求已被停止。\n");
                }
                else if (g_overall_success) {
                        g_app_state.log_output += U8("请求完成，获取到图片。\n");
                        g_app_state.total_progress_counter = g_app_state.ip_progress_total;
                }
                else {
                        g_app_state.log_output += U8("请求完成，未找到图片。\n");
                }
        }
        g_app_state.is_running = false;
}