HINSTANCE g_hInstance = nullptr;

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    g_hInstance = hModule;
    DisableThreadLibraryCalls(hModule);
  }
  return TRUE;
}
