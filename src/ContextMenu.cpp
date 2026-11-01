#include <initguid.h>
#include "ContextMenu.h"
#include <filesystem>

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "pathcch.lib")
#pragma comment(lib, "propsys.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "comctl32.lib")

// Global variables
HINSTANCE g_hInst = NULL;
LONG g_cDllRef = 0;

// Archive extensions
static const wchar_t* g_archiveExtensions[] = {
    L".7z", L".zip", L".rar", L".tar", L".gz", L".bz2",
    L".xz", L".iso", L".cab", L".arj", L".lzh", L".tgz",
    L".tbz2", L".001", L".002", L".r00", L".z01"
};

// Localized strings
static const LocalizedStrings g_stringsEN = {
    .extractHere = L"Extract Here",
    .extractTo = L"Extract to Subfolder",
    .addTo7z = L"Add to .7z",
    .addToZip = L"Add to .zip"
};

static const LocalizedStrings g_stringsCN = {
    .extractHere = L"\x63D0\x53D6\x5230\x6B64\x5904",           // 提取到此处
    .extractTo = L"\x63D0\x53D6\x5230\x5B50\x6587\x4EF6\x5939", // 提取到子文件夹
    .addTo7z = L"\x6DFB\x52A0\x5230 .7z",                       // 添加到 .7z
    .addToZip = L"\x6DFB\x52A0\x5230 .zip"                     // 添加到 .zip
};

bool IsChineseLocale()
{
    LANGID langId = GetUserDefaultUILanguage();
    WORD primaryLang = PRIMARYLANGID(langId);
    return (primaryLang == LANG_CHINESE);
}

const LocalizedStrings& GetLocalizedStrings()
{
    return IsChineseLocale() ? g_stringsCN : g_stringsEN;
}

//////////////////////////////////////////////////////////////////////////////
// CExplorerCommand implementation
//////////////////////////////////////////////////////////////////////////////

CExplorerCommand::CExplorerCommand(CommandType type)
    : m_refCount(1), m_type(type), m_enumIndex(0), m_isArchive(false), m_isDirectory(false)
{
    InterlockedIncrement(&g_cDllRef);
    if (type == CommandType::Root) {
        InitSubCommands();
    }
}

CExplorerCommand::~CExplorerCommand()
{
    for (auto cmd : m_subCommands) {
        cmd->Release();
    }
    InterlockedDecrement(&g_cDllRef);
}

void CExplorerCommand::InitSubCommands()
{
}

bool CExplorerCommand::IsArchiveFile(const std::wstring& path)
{
    std::wstring ext = PathFindExtensionW(path.c_str());
    for (const auto& archiveExt : g_archiveExtensions) {
        if (_wcsicmp(ext.c_str(), archiveExt) == 0) {
            return true;
        }
    }
    return false;
}

void CExplorerCommand::GetSelectedItems(IShellItemArray* psiItemArray)
{
    m_selectedPaths.clear();
    m_isArchive = false;
    m_isDirectory = false;
    if (!psiItemArray) return;

    DWORD count = 0;
    psiItemArray->GetCount(&count);
    for (DWORD i = 0; i < count; i++) {
        IShellItem* psi = nullptr;
        if (SUCCEEDED(psiItemArray->GetItemAt(i, &psi))) {
            PWSTR pszPath = nullptr;
            if (SUCCEEDED(psi->GetDisplayName(SIGDN_FILESYSPATH, &pszPath))) {
                m_selectedPaths.push_back(pszPath);
                CoTaskMemFree(pszPath);
            }
            psi->Release();
        }
    }
    if (!m_selectedPaths.empty()) {
        m_isArchive = IsArchiveFile(m_selectedPaths[0]);
        DWORD attrs = GetFileAttributesW(m_selectedPaths[0].c_str());
        m_isDirectory = (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY));
    }
}

IFACEMETHODIMP CExplorerCommand::QueryInterface(REFIID riid, void** ppv)
{
    static const QITAB qit[] = {
        QITABENT(CExplorerCommand, IExplorerCommand),
        QITABENT(CExplorerCommand, IEnumExplorerCommand),
        { 0 },
    };
    return QISearch(this, qit, riid, ppv);
}

IFACEMETHODIMP_(ULONG) CExplorerCommand::AddRef()
{
    return InterlockedIncrement(&m_refCount);
}

IFACEMETHODIMP_(ULONG) CExplorerCommand::Release()
{
    LONG ref = InterlockedDecrement(&m_refCount);
    if (ref == 0) {
        delete this;
    }
    return ref;
}

IFACEMETHODIMP CExplorerCommand::GetTitle(IShellItemArray* psiItemArray, LPWSTR* ppszName)
{
    const wchar_t* title = L"7-Zip";
    const auto& strings = GetLocalizedStrings();

    switch (m_type) {
        case CommandType::Root:       title = L"7-Zip"; break;
        case CommandType::ExtractHere:title = strings.extractHere; break;
        case CommandType::ExtractTo:  title = strings.extractTo; break;
        case CommandType::AddTo7z:    title = strings.addTo7z; break;
        case CommandType::AddToZip:   title = strings.addToZip; break;
    }

    return SHStrDupW(title, ppszName);
}

IFACEMETHODIMP CExplorerCommand::GetIcon(IShellItemArray* psiItemArray, LPWSTR* ppszIcon)
{
    // Use a generic archive icon from shell32.dll
    return SHStrDupW(L"shell32.dll,-171", ppszIcon);
}

IFACEMETHODIMP CExplorerCommand::GetToolTip(IShellItemArray* psiItemArray, LPWSTR* ppszInfotip)
{
    *ppszInfotip = NULL;
    return E_NOTIMPL;
}

IFACEMETHODIMP CExplorerCommand::GetCanonicalName(GUID* pguidCommandName)
{
    if (m_type == CommandType::Root) {
        *pguidCommandName = CLSID_7ZipContextMenu;
        return S_OK;
    }
    ZeroMemory(pguidCommandName, sizeof(GUID));
    return E_NOTIMPL;
}

IFACEMETHODIMP CExplorerCommand::GetState(IShellItemArray* psiItemArray, BOOL fOkToBeSlow, EXPCMDSTATE* pCmdState)
{
    GetSelectedItems(psiItemArray);

    if (m_type == CommandType::Root) {
        *pCmdState = ECS_ENABLED;
        return S_OK;
    }

    switch (m_type) {
        case CommandType::ExtractHere:
        case CommandType::ExtractTo:
            *pCmdState = (m_isArchive || !m_isDirectory) ? ECS_ENABLED : ECS_HIDDEN;
            break;

        case CommandType::AddTo7z:
        case CommandType::AddToZip:
            *pCmdState = ECS_ENABLED;
            break;

        default:
            *pCmdState = ECS_ENABLED;
            break;
    }

    return S_OK;
}

// Keep archive work outside Explorer so its lifetime and UI are independent.
static std::wstring QuoteArgument(const std::wstring& argument) {
    std::wstring result = L"\"";
    size_t slashes = 0;
    for (wchar_t c : argument) {
        if (c == L'\\') { ++slashes; continue; }
        result.append(c == L'"' ? slashes * 2 + 1 : slashes, L'\\');
        slashes = 0; result += c;
    }
    result.append(slashes * 2, L'\\');
    return result + L"\"";
}

IFACEMETHODIMP CExplorerCommand::Invoke(IShellItemArray* items, IBindCtx*) {
    GetSelectedItems(items);
    if (m_selectedPaths.empty()) return E_INVALIDARG;
    wchar_t module[32768]{};
    GetModuleFileNameW(g_hInst, module, ARRAYSIZE(module));
    auto executable = std::filesystem::path(module).parent_path() / L"7ZipContextApp.exe";
    std::wstring arguments;
    switch (m_type) {
        case CommandType::AddTo7z: arguments = L"--compress"; break;
        case CommandType::AddToZip: arguments = L"--zip"; break;
        case CommandType::ExtractHere: arguments = L"--here"; break;
        case CommandType::ExtractTo: arguments = L"--extract"; break;
        default: return E_NOTIMPL;
    }
    for (const auto& path : m_selectedPaths) arguments += L" " + QuoteArgument(path);
    std::wstring command = QuoteArgument(executable.wstring()) + L" " + arguments;
    if (command.size() >= 32767) return HRESULT_FROM_WIN32(ERROR_FILENAME_EXCED_RANGE);
    STARTUPINFOW startup{sizeof(startup)}; PROCESS_INFORMATION process{};
    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr,
        executable.parent_path().c_str(), &startup, &process)) return HRESULT_FROM_WIN32(GetLastError());
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
    return S_OK;
}

IFACEMETHODIMP CExplorerCommand::GetFlags(EXPCMDFLAGS* pFlags)
{
    if (m_type == CommandType::Root) {
        *pFlags = ECF_HASSUBCOMMANDS;
    } else {
        *pFlags = ECF_DEFAULT;
    }
    return S_OK;
}

IFACEMETHODIMP CExplorerCommand::EnumSubCommands(IEnumExplorerCommand** ppEnum)
{
    if (m_type != CommandType::Root) {
        *ppEnum = NULL;
        return E_NOTIMPL;
    }

    for (auto cmd : m_subCommands) {
        cmd->Release();
    }
    m_subCommands.clear();

    m_subCommands.push_back(new CExplorerCommand(CommandType::ExtractHere));
    m_subCommands.push_back(new CExplorerCommand(CommandType::ExtractTo));
    m_subCommands.push_back(new CExplorerCommand(CommandType::AddTo7z));
    m_subCommands.push_back(new CExplorerCommand(CommandType::AddToZip));

    m_enumIndex = 0;
    this->AddRef();
    *ppEnum = this;
    return S_OK;
}

IFACEMETHODIMP CExplorerCommand::Next(ULONG celt, IExplorerCommand** pUICommand, ULONG* pceltFetched)
{
    ULONG fetched = 0;

    while (fetched < celt && m_enumIndex < m_subCommands.size()) {
        m_subCommands[m_enumIndex]->AddRef();
        pUICommand[fetched] = m_subCommands[m_enumIndex];
        m_enumIndex++;
        fetched++;
    }

    if (pceltFetched) {
        *pceltFetched = fetched;
    }

    return (fetched == celt) ? S_OK : S_FALSE;
}

IFACEMETHODIMP CExplorerCommand::Skip(ULONG celt)
{
    m_enumIndex += celt;
    return (m_enumIndex <= m_subCommands.size()) ? S_OK : S_FALSE;
}

IFACEMETHODIMP CExplorerCommand::Reset()
{
    m_enumIndex = 0;
    return S_OK;
}

IFACEMETHODIMP CExplorerCommand::Clone(IEnumExplorerCommand** ppenum)
{
    *ppenum = NULL;
    return E_NOTIMPL;
}

//////////////////////////////////////////////////////////////////////////////
// CClassFactory implementation
//////////////////////////////////////////////////////////////////////////////

CClassFactory::CClassFactory() : m_refCount(1)
{
    InterlockedIncrement(&g_cDllRef);
}

CClassFactory::~CClassFactory()
{
    InterlockedDecrement(&g_cDllRef);
}

IFACEMETHODIMP CClassFactory::QueryInterface(REFIID riid, void** ppv)
{
    static const QITAB qit[] = {
        QITABENT(CClassFactory, IClassFactory),
        { 0 },
    };
    return QISearch(this, qit, riid, ppv);
}

IFACEMETHODIMP_(ULONG) CClassFactory::AddRef()
{
    return InterlockedIncrement(&m_refCount);
}

IFACEMETHODIMP_(ULONG) CClassFactory::Release()
{
    LONG ref = InterlockedDecrement(&m_refCount);
    if (ref == 0) {
        delete this;
    }
    return ref;
}

IFACEMETHODIMP CClassFactory::CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppv)
{
    if (pUnkOuter != NULL) {
        return CLASS_E_NOAGGREGATION;
    }

    CExplorerCommand* pCmd = new (std::nothrow) CExplorerCommand(CommandType::Root);
    if (pCmd == NULL) {
        return E_OUTOFMEMORY;
    }

    HRESULT hr = pCmd->QueryInterface(riid, ppv);
    pCmd->Release();
    return hr;
}

IFACEMETHODIMP CClassFactory::LockServer(BOOL fLock)
{
    if (fLock) {
        InterlockedIncrement(&g_cDllRef);
    } else {
        InterlockedDecrement(&g_cDllRef);
    }
    return S_OK;
}

//////////////////////////////////////////////////////////////////////////////
// DLL exports
//////////////////////////////////////////////////////////////////////////////

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv)
{
    if (!IsEqualCLSID(rclsid, CLSID_7ZipContextMenu)) {
        return CLASS_E_CLASSNOTAVAILABLE;
    }

    CClassFactory* pFactory = new (std::nothrow) CClassFactory();
    if (pFactory == NULL) {
        return E_OUTOFMEMORY;
    }

    HRESULT hr = pFactory->QueryInterface(riid, ppv);
    pFactory->Release();
    return hr;
}

STDAPI DllCanUnloadNow()
{
    return (g_cDllRef > 0) ? S_FALSE : S_OK;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    switch (ul_reason_for_call) {
        case DLL_PROCESS_ATTACH:
            g_hInst = hModule;
            DisableThreadLibraryCalls(hModule);
            break;
        case DLL_PROCESS_DETACH:
            break;
    }
    return TRUE;
}
