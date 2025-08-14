#pragma once
#include "app_state.h"
#include "graphics.h"
#include <curl/curl.h>
#include <chrono>
#include <vector>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <string>
#include <list>
#include <atomic>
#include <thread>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <set> 
#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include <string>

// ���������߳�
void StartDownloadWorkerThread(const std::string& url);

// �����̵߳�������
void DownloadWorkerThreadFunction(std::string url);

// ����CDN�����߳�
void StartCDNWorkerThread();

// CDN�����߳�������
void CDNWorkerThreadFunction();

bool RunGetCDN_Sync();

// --- libcurl���̰߳�ȫ��ʼ�� ---
static std::once_flag curl_init_flag;
static void InitCurlGlobal() {
        curl_global_init(CURL_GLOBAL_ALL); // ʹ�� CURL_GLOBAL_ALL
}

// --- HTTP��Ӧ���ݽṹ ---
struct HttpResponse {
        std::vector<char> data; // �޸�Ϊchar���㴦���ַ���
        long response_code = 0;
        std::string content_type;
        bool success = false;
};

// --- libcurl�ص����� ---
static size_t WriteCallback(void* contents, size_t size, size_t nmemb, HttpResponse* response) {
        size_t total_size = size * nmemb;
        size_t old_size = response->data.size();
        response->data.resize(old_size + total_size);
        std::memcpy(response->data.data() + old_size, contents, total_size);
        return total_size;
}
