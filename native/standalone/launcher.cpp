// A Qt-free, statically linked launcher. Windows' Cabinet API expands the full
// runtime once; the original application and its settings remain unchanged.
#include <windows.h>
#include <bcrypt.h>
#include <shlobj.h>
#include <shellapi.h>
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <fdi.h>

namespace fs = std::filesystem;
namespace {
struct Handle {
  HANDLE value = INVALID_HANDLE_VALUE;
  explicit Handle(HANDLE h = INVALID_HANDLE_VALUE) : value(h) {}
  ~Handle() { if (value != INVALID_HANDLE_VALUE && value != nullptr) CloseHandle(value); }
  Handle(const Handle&) = delete;
  Handle& operator=(const Handle&) = delete;
};

struct Resource { const unsigned char* data; DWORD size; };
Resource resource(int id) {
  auto module = GetModuleHandleW(nullptr);
  auto item = FindResourceW(module, MAKEINTRESOURCEW(id), RT_RCDATA);
  if (!item) throw std::runtime_error("Embedded runtime is missing.");
  auto data = LockResource(LoadResource(module, item));
  if (!data) throw std::runtime_error("Cannot read the embedded runtime.");
  return {static_cast<const unsigned char*>(data), SizeofResource(module, item)};
}

class Sha256 {
  BCRYPT_ALG_HANDLE algorithm_ = nullptr;
  BCRYPT_HASH_HANDLE hash_ = nullptr;
public:
  Sha256() {
    if (BCryptOpenAlgorithmProvider(&algorithm_, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
      throw std::runtime_error("SHA-256 is unavailable.");
    if (BCryptCreateHash(algorithm_, &hash_, nullptr, 0, nullptr, 0, 0) < 0) {
      BCryptCloseAlgorithmProvider(algorithm_, 0);
      throw std::runtime_error("Cannot create SHA-256 hash.");
    }
  }
  ~Sha256() { BCryptDestroyHash(hash_); BCryptCloseAlgorithmProvider(algorithm_, 0); }
  void add(const unsigned char* data, ULONG size) {
    if (BCryptHashData(hash_, const_cast<PUCHAR>(data), size, 0) < 0)
      throw std::runtime_error("Cannot hash runtime data.");
  }
  std::string finish() {
    std::array<unsigned char, 32> bytes{};
    if (BCryptFinishHash(hash_, bytes.data(), static_cast<ULONG>(bytes.size()), 0) < 0)
      throw std::runtime_error("Cannot finish runtime hash.");
    constexpr char hex[] = "0123456789abcdef";
    std::string result;
    for (auto byte : bytes) { result += hex[byte >> 4]; result += hex[byte & 15]; }
    return result;
  }
};

std::string fileHash(const fs::path& path) {
  Handle file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                          OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
  if (file.value == INVALID_HANDLE_VALUE) return {};
  Sha256 hash;
  std::array<unsigned char, 65536> buffer{};
  DWORD read = 0;
  for (;;) {
    if (!ReadFile(file.value, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr))
      return {};
    if (!read) return hash.finish();
    hash.add(buffer.data(), read);
  }
}

bool safeName(const std::string& name) {
  if (name.empty() || name.front() == '/' || name.front() == '\\') return false;
  if (name.find_first_of(":\r\n") != std::string::npos) return false;
  std::string normalized = name;
  std::replace(normalized.begin(), normalized.end(), '\\', '/');
  std::istringstream parts(normalized);
  std::string part;
  while (std::getline(parts, part, '/')) {
    if (part.empty() || part == "." || part == ".." || part.back() == '.' || part.back() == ' ')
      return false;
    for (unsigned char ch : part) if (ch < 32 || ch >= 127) return false;
  }
  return normalized.back() != '/';
}

using Manifest = std::map<std::string, std::string>;
Manifest readManifest(Resource contents) {
  Manifest entries;
  std::istringstream input(std::string(reinterpret_cast<const char*>(contents.data), contents.size));
  std::string line;
  while (std::getline(input, line)) {
    if (line.size() < 67 || line[64] != ' ' || line[65] != ' ' || !safeName(line.substr(66)))
      throw std::runtime_error("Invalid embedded runtime manifest.");
    auto digest = line.substr(0, 64);
    if (digest.find_first_not_of("0123456789abcdef") != std::string::npos ||
        !entries.emplace(line.substr(66), digest).second)
      throw std::runtime_error("Invalid embedded runtime manifest entry.");
  }
  for (const auto* required : {"SIGenerator.exe", "Qt6Core.dll", "platforms/qwindows.dll"})
    if (!entries.contains(required)) throw std::runtime_error("Incomplete embedded runtime.");
  return entries;
}

bool verified(const fs::path& root, const Manifest& entries) {
  if (!fs::is_directory(root) || fs::is_symlink(root) ||
      (GetFileAttributesW(root.c_str()) & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
  size_t count = 0;
  // Reject extra DLLs and directory links as well as changed or missing files.
  for (const auto& item : fs::recursive_directory_iterator(root)) {
    if (fs::is_symlink(item.symlink_status()) ||
        (GetFileAttributesW(item.path().c_str()) & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
    if (item.is_directory()) continue;
    const auto name = item.path().lexically_relative(root).generic_string();
    const auto expected = entries.find(name);
    if (expected == entries.end() || fileHash(item.path()) != expected->second) return false;
    ++count;
  }
  return count == entries.size();
}

struct CabinetFile { bool input; size_t offset = 0; HANDLE output = INVALID_HANDLE_VALUE; };
Resource cabinet{};
FNALLOC(allocate) { return std::malloc(cb); }
FNFREE(release) { std::free(pv); }
FNOPEN(openCabinet) {
  (void)pszFile; (void)oflag; (void)pmode;
  return reinterpret_cast<INT_PTR>(new CabinetFile{true});
}
FNREAD(readCabinet) {
  auto* file = reinterpret_cast<CabinetFile*>(hf);
  if (!file->input) return static_cast<UINT>(-1);
  const auto size = std::min<size_t>(cb, cabinet.size - file->offset);
  std::memcpy(pv, cabinet.data + file->offset, size);
  file->offset += size;
  return static_cast<UINT>(size);
}
FNWRITE(writeCabinet) {
  auto* file = reinterpret_cast<CabinetFile*>(hf);
  DWORD written = 0;
  if (file->input || !WriteFile(file->output, pv, cb, &written, nullptr)) return static_cast<UINT>(-1);
  return written;
}
FNCLOSE(closeCabinet) {
  auto* file = reinterpret_cast<CabinetFile*>(hf);
  if (!file->input) CloseHandle(file->output);
  delete file;
  return 0;
}
FNSEEK(seekCabinet) {
  auto* file = reinterpret_cast<CabinetFile*>(hf);
  const auto base = seektype == SEEK_SET ? 0LL : seektype == SEEK_CUR ?
      static_cast<long long>(file->offset) : static_cast<long long>(cabinet.size);
  const auto offset = base + dist;
  if (!file->input || offset < 0 || offset > cabinet.size) return -1;
  file->offset = static_cast<size_t>(offset);
  return static_cast<long>(offset);
}

struct Extract { fs::path root; const Manifest& manifest; std::string error; };
FNFDINOTIFY(extractNotification) {
  auto& context = *static_cast<Extract*>(pfdin->pv);
  try {
    if (fdint == fdintNEXT_CABINET || fdint == fdintPARTIAL_FILE) return -1;
    if (fdint == fdintCOPY_FILE) {
      std::string name(pfdin->psz1);
      std::replace(name.begin(), name.end(), '\\', '/');
      if (!safeName(name) || !context.manifest.contains(name))
        throw std::runtime_error("Unexpected path in embedded runtime.");
      const auto path = context.root / fs::path(name);
      fs::create_directories(path.parent_path());
      auto handle = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                 FILE_ATTRIBUTE_NORMAL, nullptr);
      if (handle == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot write runtime cache.");
      return reinterpret_cast<INT_PTR>(new CabinetFile{false, 0, handle});
    }
    if (fdint == fdintCLOSE_FILE_INFO) {
      closeCabinet(pfdin->hf);
      return TRUE;
    }
  } catch (const std::exception& error) { context.error = error.what(); return -1; }
  return 0;
}

void extract(const fs::path& target, const Manifest& manifest) {
  cabinet = resource(101);
  ERF error{};
  HFDI fdi = FDICreate(allocate, release, openCabinet, readCabinet, writeCabinet,
                       closeCabinet, seekCabinet, cpuUNKNOWN, &error);
  if (!fdi) throw std::runtime_error("Cannot initialize Windows Cabinet extraction.");
  Extract context{target, manifest, {}};
  char name[] = "runtime.cab", directory[] = "";
  const auto success = FDICopy(fdi, name, directory, 0, extractNotification, nullptr, &context);
  FDIDestroy(fdi);
  if (!success) throw std::runtime_error(context.error.empty() ?
      "Cannot expand embedded runtime. Check available disk space." : context.error);
  if (!verified(target, manifest)) throw std::runtime_error("Runtime SHA-256 verification failed.");
}

fs::path cacheRoot() {
  std::array<wchar_t, 32768> overridePath{};
  const auto length = GetEnvironmentVariableW(L"SI_GENERATOR_CACHE_ROOT", overridePath.data(),
                                              static_cast<DWORD>(overridePath.size()));
  if (length && length < overridePath.size()) {
    fs::path path(overridePath.data());
    if (!path.is_absolute()) throw std::runtime_error("SI_GENERATOR_CACHE_ROOT must be absolute.");
    return path;
  }
  PWSTR local = nullptr;
  if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &local)))
    throw std::runtime_error("Cannot locate the local application cache.");
  fs::path result = fs::path(local) / L"SI-Generator" / L"runtime";
  CoTaskMemFree(local);
  return result;
}

// Microsoft C runtime quoting, including trailing backslashes and literal quotes.
std::wstring quote(const std::wstring& argument) {
  std::wstring result = L"\"";
  size_t slashes = 0;
  for (auto ch : argument) {
    if (ch == L'\\') { ++slashes; continue; }
    result.append(slashes * (ch == L'"' ? 2 : 1), L'\\');
    slashes = 0;
    if (ch == L'"') result += L'\\';
    result += ch;
  }
  result.append(slashes * 2, L'\\');
  return result + L'"';
}

int run(int argc, wchar_t** argv) {
  const bool verifyOnly = argc == 2 && std::wstring(argv[1]) == L"--single-file-verify";
  const auto manifestData = resource(102);
  const auto manifest = readManifest(manifestData);
  Sha256 hash;
  hash.add(manifestData.data, manifestData.size);
  const auto id = hash.finish();
  const auto root = cacheRoot();
  fs::create_directories(root);
  const auto target = root / id;
  Sha256 lockHash;
  const auto rootString = fs::absolute(root).wstring();
  lockHash.add(reinterpret_cast<const unsigned char*>(rootString.data()),
               static_cast<ULONG>(rootString.size() * sizeof(wchar_t)));
  const auto lockId = lockHash.finish() + id;
  const auto mutexName = L"Local\\SI-Generator-" + std::wstring(lockId.begin(), lockId.end());
  Handle mutex(CreateMutexW(nullptr, FALSE, mutexName.c_str()));
  if (!mutex.value) throw std::runtime_error("Cannot lock the runtime cache.");
  const auto wait = WaitForSingleObject(mutex.value, 120000);
  if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED)
    throw std::runtime_error("Another SI generator is preparing the runtime. Please try again.");
  try {
    if (!verified(target, manifest)) {
      // Keep an invalid old cache intact: it might be in use by another process.
      // Build in a private directory, then atomically publish the verified cache.
      const auto staging = root / (id + ".preparing-" + std::to_string(GetCurrentProcessId()));
      if (fs::exists(staging)) throw std::runtime_error("A previous preparation folder exists. Please retry.");
      fs::create_directory(staging);
      try {
        extract(staging, manifest);
        if (fs::exists(target)) {
          const auto old = root / (id + ".previous-" + std::to_string(GetCurrentProcessId()) +
                                    "-" + std::to_string(GetTickCount64()));
          fs::rename(target, old);
        }
        fs::rename(staging, target);
      } catch (...) { fs::remove_all(staging); throw; }
    }
  } catch (...) { ReleaseMutex(mutex.value); throw; }
  ReleaseMutex(mutex.value);
  if (verifyOnly) return 0;

  const auto executable = target / L"SIGenerator.exe";
  std::wstring command = quote(executable.wstring());
  for (int i = 1; i < argc; ++i) command += L" " + quote(argv[i]);
  // Pin Qt's plugin lookup to the bundled runtime, regardless of developer Qt
  // installations or environment variables on the destination computer.
  SetEnvironmentVariableW(L"QT_PLUGIN_PATH", target.c_str());
  SetEnvironmentVariableW(L"QT_QPA_PLATFORM_PLUGIN_PATH", (target / L"platforms").c_str());
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE,
                       CREATE_UNICODE_ENVIRONMENT, nullptr, target.c_str(), &startup, &process))
    throw std::runtime_error("Cannot start SI generator from the runtime cache.");
  Handle child(process.hProcess), thread(process.hThread);
  WaitForSingleObject(child.value, INFINITE);
  DWORD code = 1;
  GetExitCodeProcess(child.value, &code);
  return static_cast<int>(code);
}
} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
  int argc = 0;
  wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  int code = 1;
  try {
    if (!argv) throw std::runtime_error("Cannot read command line.");
    code = run(argc, argv);
  } catch (const std::exception& error) {
    const std::string text = std::string("SI generator could not start.\n\n") + error.what();
    MessageBoxA(nullptr, text.c_str(), "SI generator", MB_OK | MB_ICONERROR);
  }
  if (argv) LocalFree(argv);
  return code;
}
