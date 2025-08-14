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

// 启动工作线程
void StartDownloadWorkerThread(const std::string& url);

// 工作线程的主函数
void DownloadWorkerThreadFunction(std::string url);

// 启动CDN工作线程
void StartCDNWorkerThread();

// CDN工作线程主函数
void CDNWorkerThreadFunction();

bool RunGetCDN_Sync();

// --- libcurl多线程安全初始化 ---
static std::once_flag curl_init_flag;
static void InitCurlGlobal() {
        curl_global_init(CURL_GLOBAL_ALL); // 使用 CURL_GLOBAL_ALL
}

// --- HTTP响应数据结构 ---
struct HttpResponse {
        std::vector<char> data; // 修改为char方便处理字符串
        long response_code = 0;
        std::string content_type;
        bool success = false;
};

// --- libcurl回调函数 ---
static size_t WriteCallback(void* contents, size_t size, size_t nmemb, HttpResponse* response) {
        size_t total_size = size * nmemb;
        size_t old_size = response->data.size();
        response->data.resize(old_size + total_size);
        std::memcpy(response->data.data() + old_size, contents, total_size);
        return total_size;
}
