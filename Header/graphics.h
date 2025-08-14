#pragma once
#include <Windows.h>
#include <d3d11.h>

// DirectX设备管理
bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();

// 纹理管理
bool LoadTextureFromMemory(const unsigned char* image_data, int image_size, ID3D11ShaderResourceView** out_srv, int* out_w, int* out_h);