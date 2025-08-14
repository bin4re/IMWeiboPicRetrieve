#pragma once
#include <string>

// 显示文件保存对话框
std::string ShowSaveFileDialog(const char* default_filename); 
#include "utils.h"
#include <Windows.h>
#include <commdlg.h> // For GetSaveFileNameA

std::string ShowSaveFileDialog(const char* default_filename) {
    OPENFILENAMEA ofn;
    CHAR szFile[260] = { 0 };

    if (default_filename != nullptr) {
        strncpy_s(szFile, sizeof(szFile), default_filename, _TRUNCATE);
    }

    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = GetActiveWindow();
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = "JPEG Image\0*.jpg\0PNG Image\0*.png\0All Files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = NULL;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;

    if (GetSaveFileNameA(&ofn) == TRUE) {
        std::string path = ofn.lpstrFile;
        if (ofn.nFilterIndex == 1 && path.rfind(".jpg") == std::string::npos && path.rfind(".jpeg") == std::string::npos) {
            path += ".jpg";
        }
        else if (ofn.nFilterIndex == 2 && path.rfind(".png") == std::string::npos) {
            path += ".png";
        }
        return path;
    }
    return "";
}