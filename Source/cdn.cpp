#include "worker.h"

// �� "1.2.3.4" ת��Ϊ (1, 2, 3, 4) �Ա���ȷ�Ƚ�
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
                // ����ʧ�ܣ�����һ��Ĭ��ֵ
                return std::make_tuple(0, 0, 0, 0);
        }
        catch (const std::out_of_range& oor) {
                // ����ʧ�ܣ�����һ��Ĭ��ֵ
                return std::make_tuple(0, 0, 0, 0);
        }

        if (parts.size() == 4) {
                return std::make_tuple(parts[0], parts[1], parts[2], parts[3]);
        }
        return std::make_tuple(0, 0, 0, 0);
}

// CDN �����̵߳���������������
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

// CDN IP ��ȡ���ܵĺ���ʵ��
bool RunGetCDN_Sync() {
        std::call_once(curl_init_flag, InitCurlGlobal);
        std::string domain = "wx1.sinaimg.cn";

        // ���� POST �����ȡ TaskID
        std::string task_id;
        {
                std::lock_guard<std::mutex> lock(g_app_state.mtx);
                g_app_state.log_output += U8("�������� Wss TaskID...\n");
        }
        CURL* curl = curl_easy_init();
        if (!curl) {
                std::lock_guard<std::mutex> lock(g_app_state.mtx);
                g_app_state.log_output += U8("����: curl_easy_init() ʧ�ܡ�\n");
                return false;
        }

        // ������utf-8���룬�������������ʧ��
        HttpResponse post_response;
        std::u8string isps_utf8 = u8"����";
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
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L); // ����֤����֤������ requests(verify=False)
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

        CURLcode res = curl_easy_perform(curl);
        curl_slist_free_all(headers);

        if (res != CURLE_OK) {
                std::lock_guard<std::mutex> lock(g_app_state.mtx);
                g_app_state.log_output += U8("����: ����ʧ��: ") + std::string(curl_easy_strerror(res)) + U8("\n");
                curl_easy_cleanup(curl);
                return false;
        }

        // ���� JSON ��Ӧ�Ի�ȡ TaskID
        rapidjson::Document doc;
        post_response.data.push_back('\0'); // ȷ���ַ�����ȷ��ֹ
        doc.Parse(post_response.data.data());

        if (doc.HasParseError() || !doc.IsObject() || !doc.HasMember("Message") || !doc.HasMember("Data")) {
                std::lock_guard<std::mutex> lock(g_app_state.mtx);
                g_app_state.log_output += U8("����: ���� JSON ��Ӧʧ�ܡ�\n");
                curl_easy_cleanup(curl);
                return false;
        }

        if (std::string(doc["Message"].GetString()) != "ok" || !doc["Data"].IsObject() || !doc["Data"].HasMember("TaskID")) {
                std::lock_guard<std::mutex> lock(g_app_state.mtx);
                g_app_state.log_output += U8("����: ��������δ�ɹ�: ") + std::string(doc["Message"].GetString()) + U8("\n");
                curl_easy_cleanup(curl);
                return false;
        }
        task_id = doc["Data"]["TaskID"].GetString();
        curl_easy_cleanup(curl); // ����POST����ľ��
        {
                std::lock_guard<std::mutex> lock(g_app_state.mtx);
                g_app_state.log_output += U8("��ȡ�� TaskID: ") + task_id + U8("\n");
        }

        // ���� WebSocket ����������
        g_app_state.log_output += U8("----------------------------------------\n");
        std::set<std::string> new_ips;
        {
                std::lock_guard<std::mutex> lock(g_app_state.mtx);
                g_app_state.log_output += U8("�������� WebSocket ������ CDN IP...\n");
        }

        CURL* ws_curl = curl_easy_init();
        if (!ws_curl) return false;

        std::string ws_url = "wss://zhale.me/ws/0/" + task_id;
        curl_easy_setopt(ws_curl, CURLOPT_URL, ws_url.c_str());
        curl_easy_setopt(ws_curl, CURLOPT_CONNECT_ONLY, 2L); // ֻ��������

        res = curl_easy_perform(ws_curl);
        if (res != CURLE_OK) {
                std::lock_guard<std::mutex> lock(g_app_state.mtx);
                g_app_state.log_output += U8("����: WebSocket ����ʧ��: ") + std::string(curl_easy_strerror(res)) + U8("\n");
                curl_easy_cleanup(ws_curl);
                return false;
        }

        auto start_time = std::chrono::steady_clock::now();
        size_t rlen;
        char buffer[2048];

        while (std::chrono::steady_clock::now() - start_time < std::chrono::seconds(15)) {
                const struct curl_ws_frame* meta;
                res = curl_ws_recv(ws_curl, buffer, sizeof(buffer) - 1, &rlen, &meta);
                if (res == CURLE_AGAIN) continue; // û�����ݣ������ȴ�
                if (res != CURLE_OK) break; // ���������ر�����

                buffer[rlen] = '\0'; // ��ֹ�ַ���

                rapidjson::Document ws_doc;
                ws_doc.Parse(buffer);
                if (!ws_doc.HasParseError() && ws_doc.IsObject() && ws_doc.HasMember("Result")) {
                        const auto& result = ws_doc["Result"];
                        if (result.IsObject() && result.HasMember("IP") && result["IP"].IsString()) {
                                // ��ȡIP
                                const char* ip = result["IP"].GetString();

                                // ��ȡ������Ϣ������ֶβ�������ʹ�� "N/A" ��ΪĬ��ֵ
                                const char* country = (result.HasMember("Country") && result["Country"].IsString()) ? result["Country"].GetString() : "N/A";
                                const char* region = (result.HasMember("Region") && result["Region"].IsString()) ? result["Region"].GetString() : "N/A";
                                const char* isp = (result.HasMember("ISP") && result["ISP"].IsString()) ? result["ISP"].GetString() : "N/A";

                                // ����ȡ����IP����set�У����ں���д���ļ�
                                new_ips.insert(ip);

                                // ������ϸ����־��Ϣ
                                std::string log_message = U8("�յ� IP: ") + std::string(ip) +
                                        U8(" | ����: ") + std::string(country) +
                                        U8(" | ����: ") + std::string(region) +
                                        U8(" | ISP: ") + std::string(isp) + U8("\n");

                                // �����԰�ȫ�ظ���UI��־
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
                g_app_state.log_output += U8("���ݽ�����ϣ��յ� ") + std::to_string(new_ips.size()) + U8(" ����IP��\n");
        }

        if (new_ips.empty()) {
                std::lock_guard<std::mutex> lock(g_app_state.mtx);
                g_app_state.log_output += U8("����: δ�� WebSocket ��ȡ���κ���IP��\n");
        }

        // ��ȡ���� IP ���ϲ�
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
                g_app_state.log_output += U8("�ϲ��ļ���IP���ܼ� ") + std::to_string(all_ips.size()) + U8(" �� IP����ʼ��Ч�Լ��...\n");
        }

        // �������IP��Ч��
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
                        curl_easy_setopt(eh, CURLOPT_NOBODY, 1L); // ֻ����ͷ
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

        // �������� ips.txt
        auto ipv4_comparator = [](const std::string& a, const std::string& b) {
                return Ipv4ToTuple(a) < Ipv4ToTuple(b);
                };
        std::sort(valid_ips.begin(), valid_ips.end(), ipv4_comparator);

        std::ofstream ip_file_out("ips.txt");
        if (ip_file_out.is_open()) {
                for (const auto& ip : valid_ips) ip_file_out << ip << "\n";
                ip_file_out.close();

                std::lock_guard<std::mutex> lock(g_app_state.mtx);

                g_app_state.log_output += U8("IP�����ɣ���Ч: ") + std::to_string(valid_ips.size()) + U8("����\n");
                g_app_state.log_output += U8("ips.txt �ļ��Ѹ��¡�\n");
        }
        else {
                std::lock_guard<std::mutex> lock(g_app_state.mtx);
                g_app_state.log_output += U8("����: �޷��� ips.txt �ļ�����д�롣\n");
        }
        return true;
}


