#pragma once

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shlwapi.h>
#include <strsafe.h>
#include <string>
#include <vector>

// GUIDs for our commands - declared here, defined in cpp
// {B8A0B7C1-7C5D-4B3A-9E1F-2A3B4C5D6E7F}
DEFINE_GUID(CLSID_7ZipContextMenu, 0xb8a0b7c1, 0x7c5d, 0x4b3a, 0x9e, 0x1f, 0x2a, 0x3b, 0x4c, 0x5d, 0x6e, 0x7f);

// Command types
enum class CommandType {
    Root,           // Root menu item "7-Zip"
    ExtractHere,    // Extract Here
    ExtractTo,      // Extract to subfolder
    AddTo7z,        // Add to .7z
    AddToZip        // Add to .zip
};

// Localized strings
struct LocalizedStrings {
    const wchar_t* extractHere;
    const wchar_t* extractTo;
    const wchar_t* addTo7z;
    const wchar_t* addToZip;
};

// Get system language and return appropriate strings
bool IsChineseLocale();
const LocalizedStrings& GetLocalizedStrings();

// Forward declarations
class CExplorerCommand;
class CEnumExplorerCommand;

// Base Explorer Command class
class CExplorerCommand : public IExplorerCommand, public IEnumExplorerCommand
{
public:
    CExplorerCommand(CommandType type);
    virtual ~CExplorerCommand();

    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv);
    IFACEMETHODIMP_(ULONG) AddRef();
    IFACEMETHODIMP_(ULONG) Release();

    // IExplorerCommand
    IFACEMETHODIMP GetTitle(IShellItemArray* psiItemArray, LPWSTR* ppszName);
    IFACEMETHODIMP GetIcon(IShellItemArray* psiItemArray, LPWSTR* ppszIcon);
    IFACEMETHODIMP GetToolTip(IShellItemArray* psiItemArray, LPWSTR* ppszInfotip);
    IFACEMETHODIMP GetCanonicalName(GUID* pguidCommandName);
    IFACEMETHODIMP GetState(IShellItemArray* psiItemArray, BOOL fOkToBeSlow, EXPCMDSTATE* pCmdState);
    IFACEMETHODIMP Invoke(IShellItemArray* psiItemArray, IBindCtx* pbc);
    IFACEMETHODIMP GetFlags(EXPCMDFLAGS* pFlags);
    IFACEMETHODIMP EnumSubCommands(IEnumExplorerCommand** ppEnum);

    // IEnumExplorerCommand
    IFACEMETHODIMP Next(ULONG celt, IExplorerCommand** pUICommand, ULONG* pceltFetched);
    IFACEMETHODIMP Skip(ULONG celt);
    IFACEMETHODIMP Reset();
    IFACEMETHODIMP Clone(IEnumExplorerCommand** ppenum);

private:
    LONG m_refCount;
    CommandType m_type;
    ULONG m_enumIndex;
    std::vector<IExplorerCommand*> m_subCommands;
    std::vector<std::wstring> m_selectedPaths;
    bool m_isArchive;
    bool m_isDirectory;

    void InitSubCommands();
    void GetSelectedItems(IShellItemArray* psiItemArray);
    bool IsArchiveFile(const std::wstring& path);
};

// Class factory
class CClassFactory : public IClassFactory
{
public:
    CClassFactory();
    virtual ~CClassFactory();

    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv);
    IFACEMETHODIMP_(ULONG) AddRef();
    IFACEMETHODIMP_(ULONG) Release();

    // IClassFactory
    IFACEMETHODIMP CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppv);
    IFACEMETHODIMP LockServer(BOOL fLock);

private:
    LONG m_refCount;
};

// DLL exports
STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv);
STDAPI DllCanUnloadNow();
STDAPI DllRegisterServer();
STDAPI DllUnregisterServer();

// Global variables
extern HINSTANCE g_hInst;
extern LONG g_cDllRef;
