#pragma once

// COM smart pointer used across the app. WRL ships with the Windows SDK, so
// building needs no extra Visual Studio component (unlike ATL's CComPtr).
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;
