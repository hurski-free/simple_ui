#pragma once

// Lightweight platform forwards so clients do not need <windows.h> / <d3d11.h>
// just to include simple_ui.h. Full types are provided by those headers when needed.

#ifndef SIMPLE_UI_HWND_DEFINED
#ifdef _WINDEF_
#define SIMPLE_UI_HWND_DEFINED
#else
struct HWND__;
typedef struct HWND__* HWND;
#define SIMPLE_UI_HWND_DEFINED
#endif
#endif

#ifndef __ID3D11Device_FWD_DEFINED__
#define __ID3D11Device_FWD_DEFINED__
typedef struct ID3D11Device ID3D11Device;
#endif

#ifndef __ID3D11DeviceContext_FWD_DEFINED__
#define __ID3D11DeviceContext_FWD_DEFINED__
typedef struct ID3D11DeviceContext ID3D11DeviceContext;
#endif
