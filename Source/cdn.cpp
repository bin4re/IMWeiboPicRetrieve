#include "worker.h"

// 将 "1.2.3.4" 转换为 (1, 2, 3, 4) 以便正确比较
std::tuple<int, int, int, int> Ipv4ToTuple(const std::string& ip_str) {
        std::stringstream ss(ip_str);
        std::string segment;
        std::vector<int> parts;
        try {
                while (std::getline(ss, segment, '.')) {
                        parts.push_back(std::stoi(segment));
                }
        }
        catch (const std::invalid_argument& ia) {
                // 解析失败，返回一个默认值
                return std::make_tuple(0, 0, 0, 0);
        }
        catch (const std::out_of_range& oor) {
                // 解析失败，返回一个默认值
                return std::make_tuple(0, 0, 0, 0);
        }

        if (parts.size() == 4) {
                return std::make_tuple(parts[0], parts[1], parts[2], parts[3]);
        }
        return std::make_tuple(0, 0, 0, 0);
}

// CDN 工作线程的启动器和主函数
void StartCDNWorkerThread() {
        if (g_cdn_worker_thread.joinable()) {
                g_cdn_worker_thread.join();
        }
        g_app_state.log_output.clear();
        g_app_state.log_output.reserve(16384);
        g_app_state.is_cdn_running = true;
        g_cdn_worker_thread = std::thread(CDNWorkerThreadFunction);
}

void CDNWorkerThreadFunction() {
        RunGetCDN_Sync();
        g_app_state.is_cdn_running = false;
}

// CDN IP 获取功能的核心实现
bool RunGetCDN_Sync() {
        std::call_once(curl_init_flag, InitCurlGlobal);
        std::string domain = "wx1.sinaimg.cn";

        // 发送 POST 请求获取 TaskID
        std::string task_id;
        {
                std::lock_guard<std::mutex> lock(g_app_state.mtx);
                g_app_state.log_output += U8("正在请求 Wss TaskID...\n");
        }
        CURL* curl = curl_easy_init();
        if (!curl) {
                std::lock_guard<std::mutex> lock(g_app_state.mtx);
                g_app_state.log_output += U8("错误: curl_easy_init() 失败。\n");
                return false;
        }

        // 请求是utf-8编码，中文乱码会请求失败
        HttpResponse post_response;
        std::u8string isps_utf8 = u8"海外";
        std::string post_data = std::format(
                R"({{"Target": "wx1.sinaimg.cn", "Options": {{"ISPs": ["{}"]}}, "IsContinue": false}})",
                std::string_view(reinterpret_cast<const char*>(isps_utf8.data()), isps_utf8.size())
        );
        struct curl_slist* headers = NULL;
        headers = curl_slist_append(headers, "Content-Type: application/json");

        curl_easy_setopt(curl, CURLOPT_URL, "https://zhale.me/v1/ping/new");
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &post_response);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L); // 禁用证书验证，类似 requests(verify=False)
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

        CURLcode res = curl_easy_perform(curl);
        curl_slist_free_all(headers);

        if (res != CURLE_OK) {
                std::lock_guard<std::mutex> lock(g_app_state.mtx);
                g_app_state.log_output += U8("错误: 请求失败: ") + std::string(curl_easy_strerror(res)) + U8("\n");
                curl_easy_cleanup(curl);
                return false;
        }

        // 解析 JSON 响应以获取 TaskID
        rapidjson::Document doc;
        post_response.data.push_back('\0'); // 确保字符串正确终止
        doc.Parse(post_response.data.data());

        if (doc.HasParseError() || !doc.IsObject() || !doc.HasMember("Message") || !doc.HasMember("Data")) {
                std::lock_guard<std::mutex> lock(g_app_state.mtx);
                g_app_state.log_output += U8("错误: 解析 JSON 响应失败。\n");
                curl_easy_cleanup(curl);
                return false;
        }

        if (std::string(doc["Message"].GetString()) != "ok" || !doc["Data"].IsObject() || !doc["Data"].HasMember("TaskID")) {
                std::lock_guard<std::mutex> lock(g_app_state.mtx);
                g_app_state.log_output += U8("错误: 任务请求未成功: ") + std::string(doc["Message"].GetString()) + U8("\n");
                curl_easy_cleanup(curl);
                return false;
        }
        task_id = doc["Data"]["TaskID"].GetString();
        curl_easy_cleanup(curl); // 清理POST请求的句柄
        {
                std::lock_guard<std::mutex> lock(g_app_state.mtx);
                g_app_state.log_output += U8("获取到 TaskID: ") + task_id + U8("\n");
        }

        // 连接 WebSocket 并接收数据
        g_app_state.log_output += U8("----------------------------------------\n");
        std::set<std::string> new_ips;
        {
                std::lock_guard<std::mutex> lock(g_app_state.mtx);
                g_app_state.log_output += U8("正在连接 WebSocket 并接收 CDN IP...\n");
        }

        CURL* ws_curl = curl_easy_init();
        if (!ws_curl) return false;

        std::string ws_url = "wss://zhale.me/ws/0/" + task_id;
        curl_easy_setopt(ws_curl, CURLOPT_URL, ws_url.c_str());
        curl_easy_setopt(ws_curl, CURLOPT_CONNECT_ONLY, 2L); // 只建立连接

        res = curl_easy_perform(ws_curl);
        if (res != CURLE_OK) {
                std::lock_guard<std::mutex> lock(g_app_state.mtx);
                g_app_state.log_output += U8("错误: WebSocket 连接失败: ") + std::string(curl_easy_strerror(res)) + U8("\n");
                curl_easy_cleanup(ws_curl);
                return false;
        }

        auto start_time = std::chrono::steady_clock::now();
        size_t rlen;
        char buffer[2048];

        while (std::chrono::steady_clock::now() - start_time < std::chrono::seconds(15)) {
                const struct curl_ws_frame* meta;
                res = curl_ws_recv(ws_curl, buffer, sizeof(buffer) - 1, &rlen, &meta);
                if (res == CURLE_AGAIN) continue; // 没有数据，继续等待
                if (res != CURLE_OK) break; // 发生错误或关闭连接

                buffer[rlen] = '\0'; // 终止字符串

                rapidjson::Document ws_doc;
                ws_doc.Parse(buffer);
                if (!ws_doc.HasParseError() && ws_doc.IsObject() && ws_doc.HasMember("Result")) {
                        const auto& result = ws_doc["Result"];
                        if (result.IsObject() && result.HasMember("IP") && result["IP"].IsString()) {
                                // 提取IP
                                const char* ip = result["IP"].GetString();

                                // 提取其他信息，如果字段不存在则使用 "N/A" 作为默认值
                                const char* country = (result.HasMember("Country") && result["Country"].IsString()) ? result["Country"].GetString() : "N/A";
                                const char* region = (result.HasMember("Region") && result["Region"].IsString()) ? result["Region"].GetString() : "N/A";
                                const char* isp = (result.HasMember("ISP") && result["ISP"].IsString()) ? result["ISP"].GetString() : "N/A";

                                // 将获取到的IP存入set中，用于后续写入文件
                                new_ips.insert(ip);

                                // 构建详细的日志消息
                                std::string log_message = U8("收到 IP: ") + std::string(ip) +
                                        U8(" | 国家: ") + std::string(country) +
                                        U8(" | 地区: ") + std::string(region) +
                                        U8(" | ISP: ") + std::string(isp) + U8("\n");

                                // 加锁以安全地更新UI日志
                                {
                                        std::lock_guard<std::mutex> lock(g_app_state.mtx);
                                        g_app_state.log_output += log_message;
                                }
                        }
                }
        }
        curl_easy_cleanup(ws_curl);

        {
                std::lock_guard<std::mutex> lock(g_app_state.mtx);
                g_app_state.log_output += U8("数据接收完毕，收到 ") + std::to_string(new_ips.size()) + U8(" 个新IP。\n");
        }

        if (new_ips.empty()) {
                std::lock_guard<std::mutex> lock(g_app_state.mtx);
                g_app_state.log_output += U8("警告: 未从 WebSocket 获取到任何新IP。\n");
        }

        // 读取本地 IP 并合并
        g_app_state.log_output += U8("----------------------------------------\n");
        std::set<std::string> ip_set;
        for (const auto& ip : new_ips) ip_set.insert(ip);

        std::ifstream ip_file_in("ips.txt");
        std::string line;
        while (std::getline(ip_file_in, line)) {
                if (!line.empty()) ip_set.insert(line);
        }
        ip_file_in.close();

        std::vector<std::string> all_ips(ip_set.begin(), ip_set.end());
        {
                std::lock_guard<std::mutex> lock(g_app_state.mtx);
                g_app_state.log_output += U8("合并文件中IP后总计 ") + std::to_string(all_ips.size()) + U8(" 个 IP，开始有效性检查...\n");
        }

        // 并发检查IP有效性
        g_app_state.total_progress_counter = 0;
        g_app_state.ip_progress_total = all_ips.size();

        std::vector<std::string> valid_ips;
        std::mutex result_mutex;

        CURLM* multi_handle = curl_multi_init();
        std::vector<CURL*> easy_handles;

        for (const auto& ip : all_ips) {
                CURL* eh = curl_easy_init();
                if (eh) {
                        std::string url = "https://" + ip;
                        struct curl_slist* headers = NULL;
                        headers = curl_slist_append(headers, "Host: wx1.sinaimg.cn");

                        curl_easy_setopt(eh, CURLOPT_URL, url.c_str());
                        curl_easy_setopt(eh, CURLOPT_HTTPHEADER, headers);
                        curl_easy_setopt(eh, CURLOPT_NOBODY, 1L); // 只请求头
                        curl_easy_setopt(eh, CURLOPT_TIMEOUT, 5L);
                        curl_easy_setopt(eh, CURLOPT_SSL_VERIFYPEER, 0L);
                        curl_easy_setopt(eh, CURLOPT_SSL_VERIFYHOST, 0L);
                        curl_easy_setopt(eh, CURLOPT_PRIVATE, (void*)ip.c_str());

                        curl_multi_add_handle(multi_handle, eh);
                        easy_handles.push_back(eh);
                }
        }

        int still_running = 1;
        while (still_running) {
                curl_multi_perform(multi_handle, &still_running);
                CURLMsg* msg;
                int msgs_left;
                while ((msg = curl_multi_info_read(multi_handle, &msgs_left))) {
                        if (msg->msg == CURLMSG_DONE) {
                                char* ip_char = nullptr;
                                curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, &ip_char);
                                std::string ip_str = ip_char;

                                long response_code = 0;
                                if (msg->data.result == CURLE_OK) {
                                        curl_easy_getinfo(msg->easy_handle, CURLINFO_RESPONSE_CODE, &response_code);
                                }

                                {
                                        std::lock_guard<std::mutex> lock(result_mutex);
                                        if (response_code == 200) {
                                                valid_ips.push_back(ip_str);
                                        }
                                        //else {
                                        //    invalid_ips.push_back(ip_str);
                                        //}
                                }
                                g_app_state.total_progress_counter++;
                                curl_multi_remove_handle(multi_handle, msg->easy_handle);
                        }
                }
                if (still_running) {
                        curl_multi_wait(multi_handle, NULL, 0, 100, NULL);
                }
        }

        for (CURL* eh : easy_handles) curl_easy_cleanup(eh);
        curl_multi_cleanup(multi_handle);

        // 保存结果到 ips.txt
        auto ipv4_comparator = [](const std::string& a, const std::string& b) {
                return Ipv4ToTuple(a) < Ipv4ToTuple(b);
                };
        std::sort(valid_ips.begin(), valid_ips.end(), ipv4_comparator);

        std::ofstream ip_file_out("ips.txt");
        if (ip_file_out.is_open()) {
                for (const auto& ip : valid_ips) ip_file_out << ip << "\n";
                ip_file_out.close();

                std::lock_guard<std::mutex> lock(g_app_state.mtx);

                g_app_state.log_output += U8("IP检查完成！有效: ") + std::to_string(valid_ips.size()) + U8("个。\n");
                g_app_state.log_output += U8("ips.txt 文件已更新。\n");
        }
        else {
                std::lock_guard<std::mutex> lock(g_app_state.mtx);
                g_app_state.log_output += U8("错误: 无法打开 ips.txt 文件进行写入。\n");
        }
        return true;
}


