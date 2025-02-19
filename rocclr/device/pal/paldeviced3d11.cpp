/* Copyright (c) 2015 - 2021 Advanced Micro Devices, Inc.

 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in
 all copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 THE SOFTWARE. */

#include "paldevice.hpp"

#if defined(ATI_OS_LINUX)
namespace amd::pal {
bool Device::associateD3D11Device(void* d3d11Device) { return false; }
}  // namespace amd::pal
#else  // !ATI_OS_LINUX

#include <D3D11.h>

/**************************************************************************************************************
 * Note: ideally the DXX extension interfaces should be mapped from the DXX perforce branch.
 * This means OCL client spec will need to change to include headers directly from the DXX perforce
 *tree.
 * However, OCL only cares about the DXX OpenCL extension interface class. The spec cannot change
 * without notification. So it is safe to use a local copy of the relevant DXX extension interface
 *classes.
 **************************************************************************************************************/
#include "DxxOpenCLInteropExt.h"

namespace amd::pal {

static bool queryD3D11DeviceGPUMask(ID3D11Device* pd3d11Device, UINT* pd3d11DeviceGPUMask) {
  IAmdDxExt* pExt = nullptr;
  IAmdDxExtCLInterop* pCLExt = nullptr;
  PFNAmdDxExtCreate11 AmdDxExtCreate11;
  HRESULT hr = S_OK;

// Get a handle to the DXX DLL with extension API support
#if defined _WIN64
  static constexpr CHAR dxxModuleName[13] = "atidxx64.dll";
#else
  static constexpr CHAR dxxModuleName[13] = "atidxx32.dll";
#endif

  HMODULE hDLL = GetModuleHandle(dxxModuleName);

  if (hDLL == nullptr) {
    hr = E_FAIL;
  }

  // Get the exported AmdDxExtCreate() function pointer
  if (SUCCEEDED(hr)) {
    AmdDxExtCreate11 =
        reinterpret_cast<PFNAmdDxExtCreate11>(GetProcAddress(hDLL, "AmdDxExtCreate11"));
    if (AmdDxExtCreate11 == nullptr) {
      hr = E_FAIL;
    }
  }

  // Create the extension object
  if (SUCCEEDED(hr)) {
    hr = AmdDxExtCreate11(pd3d11Device, &pExt);
  }

  // Get the extension version information
  if (SUCCEEDED(hr)) {
    AmdDxExtVersion extVersion;
    hr = pExt->GetVersion(&extVersion);

    if (extVersion.majorVersion == 0) {
      hr = E_FAIL;
    }
  }

  // Get the OpenCL Interop interface
  if (SUCCEEDED(hr)) {
    pCLExt = static_cast<IAmdDxExtCLInterop*>(pExt->GetExtInterface(AmdDxExtCLInteropID));
    if (pCLExt != nullptr) {
      // Get the GPU mask using the CL Interop extension.
      pCLExt->QueryInteropGpuMask(pd3d11DeviceGPUMask);
    } else {
      hr = E_FAIL;
    }
  }

  if (pCLExt != nullptr) {
    pCLExt->Release();
  }

  if (pExt != nullptr) {
    pExt->Release();
  }

  return (SUCCEEDED(hr));
}

bool Device::associateD3D11Device(void* d3d11Device) {
  ID3D11Device* pd3d11Device = static_cast<ID3D11Device*>(d3d11Device);

  IDXGIDevice* pDXGIDevice;
  pd3d11Device->QueryInterface(__uuidof(IDXGIDevice), (void**)&pDXGIDevice);

  IDXGIAdapter* pDXGIAdapter;
  pDXGIDevice->GetAdapter(&pDXGIAdapter);

  DXGI_ADAPTER_DESC adapterDesc;
  pDXGIAdapter->GetDesc(&adapterDesc);

  // match the adapter
  bool canInteroperate =
      (properties().osProperties.luidHighPart == adapterDesc.AdapterLuid.HighPart) &&
      (properties().osProperties.luidLowPart == adapterDesc.AdapterLuid.LowPart);

  UINT chainBitMask = 1 << properties().gpuIndex;

  // match the chain ID
  if (canInteroperate) {
    UINT d3d11DeviceGPUMask = 0;

    if (queryD3D11DeviceGPUMask(pd3d11Device, &d3d11DeviceGPUMask)) {
      canInteroperate = (chainBitMask & d3d11DeviceGPUMask) != 0;
    } else {
      // special handling for Intel iGPU + AMD dGPU in LDA mode
      // (only occurs on a PX platform) where
      // the D3D11Device object is created on the Intel iGPU and
      // passed to AMD dGPU (secondary) to interoperate.
      if (chainBitMask > 1) {
        canInteroperate = false;
      }
    }
  }

  pDXGIDevice->Release();
  pDXGIAdapter->Release();

  return canInteroperate;
}

}  // namespace amd::pal

#endif  // !ATI_OS_LINUX
