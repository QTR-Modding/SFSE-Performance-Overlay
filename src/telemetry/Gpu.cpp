#include "Gpu.h"
#include "NvmlApi.h"

#include <Windows.h>
#include <winternl.h>
#include <d3dkmthk.h>
#include <dxgi1_4.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <shlobj.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cwctype>
#include <map>
#include <string_view>

namespace Overlay::Telemetry
{
    namespace
    {
        constexpr double GiB = 1024.0 * 1024.0 * 1024.0;
        constexpr unsigned int MaxAdapters = 16;
        using Microsoft::WRL::ComPtr;

        template<class Function>
        Function Resolve(HMODULE module, const char* name)
        {
            return reinterpret_cast<Function>(GetProcAddress(module, name));
        }

        HMODULE LoadNvml()
        {
            if (auto module = LoadLibraryExW(L"nvml.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32)) {
                return module;
            }
            PWSTR folder = nullptr;
            if (FAILED(SHGetKnownFolderPath(FOLDERID_ProgramFilesX64, 0, nullptr, &folder))) {
                return nullptr;
            }
            const std::wstring path = std::wstring(folder) + L"\\NVIDIA Corporation\\NVSMI\\nvml.dll";
            CoTaskMemFree(folder);
            return LoadLibraryExW(path.c_str(), nullptr,
                LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        }

        std::string Utf8(const wchar_t* name)
        {
            const auto size = WideCharToMultiByte(CP_UTF8, 0, name, -1, nullptr, 0, nullptr, nullptr);
            if (size <= 1) {
                return "Graphics adapter";
            }
            std::string result(static_cast<std::size_t>(size), '\0');
            WideCharToMultiByte(CP_UTF8, 0, name, -1, result.data(), size, nullptr, nullptr);
            result.pop_back();
            return result;
        }

        std::optional<D3DKMT_ADAPTERADDRESS> PciAddress(LUID luid)
        {
            D3DKMT_OPENADAPTERFROMLUID open{};
            open.AdapterLuid = luid;
            if (D3DKMTOpenAdapterFromLuid(&open) < 0) {
                return std::nullopt;
            }
            D3DKMT_ADAPTERADDRESS address{};
            D3DKMT_QUERYADAPTERINFO query{};
            query.hAdapter = open.hAdapter;
            query.Type = KMTQAITYPE_ADAPTERADDRESS;
            query.pPrivateDriverData = &address;
            query.PrivateDriverDataSize = sizeof(address);
            const auto status = D3DKMTQueryAdapterInfo(&query);
            D3DKMT_CLOSEADAPTER close{open.hAdapter};
            D3DKMTCloseAdapter(&close);
            return status >= 0 ? std::optional(address) : std::nullopt;
        }

        struct CounterSample { std::wstring name; double value; };

        std::vector<CounterSample> ReadCounter(PDH_HCOUNTER counter)
        {
            std::vector<CounterSample> samples;
            if (!counter) {
                return samples;
            }
            DWORD bytes = 0, count = 0;
            constexpr DWORD format = PDH_FMT_DOUBLE | PDH_FMT_NOCAP100;
            if (PdhGetFormattedCounterArrayW(counter, format, &bytes, &count, nullptr) != PDH_MORE_DATA ||
                bytes == 0 || bytes > 8 * 1024 * 1024) {
                return samples;
            }
            std::vector<unsigned char> buffer(bytes);
            auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buffer.data());
            if (PdhGetFormattedCounterArrayW(counter, format, &bytes, &count, items) != ERROR_SUCCESS ||
                count > 65536 || count > buffer.size() / sizeof(*items)) {
                return samples;
            }
            samples.reserve(count);
            for (DWORD i = 0; i < count; ++i) {
                const auto& value = items[i].FmtValue;
                if ((value.CStatus == PDH_CSTATUS_VALID_DATA || value.CStatus == PDH_CSTATUS_NEW_DATA) &&
                    items[i].szName && std::isfinite(value.doubleValue) && value.doubleValue >= 0) {
                    // PDH emits uppercase hex LUIDs; DXGI matching uses lowercase.
                    std::wstring name(items[i].szName);
                    std::ranges::transform(name, name.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
                    samples.push_back({std::move(name), value.doubleValue});
                }
            }
            return samples;
        }
    }

    struct GpuCollector::Impl
    {
        struct Adapter
        {
            ComPtr<IDXGIAdapter3> dxgi;
            DXGI_ADAPTER_DESC1 description{};
            std::wstring counterLuid;
            Nvml::Device nvml = nullptr;
        };

        std::vector<Adapter> adapters;
        HMODULE nvml = nullptr;
        bool initialized = false;
        Nvml::Shutdown shutdown = nullptr;
        Nvml::Usage usage = nullptr;
        Nvml::Temperature temperature = nullptr;
        Nvml::Power power = nullptr;
        Nvml::Clock clock = nullptr;
        Nvml::MemoryInfo memory = nullptr;
        PDH_HQUERY query = nullptr;
        PDH_HCOUNTER engines = nullptr, dedicated = nullptr;

        Impl()
        {
            ComPtr<IDXGIFactory1> factory;
            if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
                for (unsigned int i = 0; i < MaxAdapters; ++i) {
                    ComPtr<IDXGIAdapter1> adapter;
                    if (factory->EnumAdapters1(i, &adapter) == DXGI_ERROR_NOT_FOUND) {
                        break;
                    }
                    Adapter entry;
                    if (!adapter || FAILED(adapter->GetDesc1(&entry.description)) ||
                        (entry.description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) || FAILED(adapter.As(&entry.dxgi))) {
                        continue;
                    }
                    wchar_t luid[64]{};
                    swprintf_s(luid, L"luid_0x%08x_0x%08x_", static_cast<unsigned int>(entry.description.AdapterLuid.HighPart),
                        entry.description.AdapterLuid.LowPart);
                    entry.counterLuid = luid;
                    adapters.push_back(std::move(entry));
                }
            }
            InitializeNvml();
            if (PdhOpenQueryW(nullptr, 0, &query) == ERROR_SUCCESS) {
                if (PdhAddEnglishCounterW(query, L"\\GPU Engine(*)\\Utilization Percentage", 0, &engines) != ERROR_SUCCESS) {
                    engines = nullptr;
                }
                if (PdhAddEnglishCounterW(query, L"\\GPU Adapter Memory(*)\\Dedicated Usage", 0, &dedicated) != ERROR_SUCCESS) {
                    dedicated = nullptr;
                }
                PdhCollectQueryData(query); // Rate counters require a preceding sample.
            }
        }

        ~Impl()
        {
            if (query) {
                PdhCloseQuery(query);
            }
            if (initialized) {
                shutdown();
            }
            if (nvml) {
                FreeLibrary(nvml);
            }
        }

        void InitializeNvml()
        {
            if (std::ranges::none_of(adapters, [](const Adapter& a) { return a.description.VendorId == 0x10DE; })) {
                return;
            }
            nvml = LoadNvml();
            if (!nvml) {
                return;
            }
            const auto initialize = Resolve<Nvml::Initialize>(nvml, "nvmlInit_v2");
            shutdown = Resolve<Nvml::Shutdown>(nvml, "nvmlShutdown");
            const auto count = Resolve<Nvml::Count>(nvml, "nvmlDeviceGetCount_v2");
            const auto handle = Resolve<Nvml::Handle>(nvml, "nvmlDeviceGetHandleByIndex_v2");
            const auto pci = Resolve<Nvml::Pci>(nvml, "nvmlDeviceGetPciInfo_v3");
            if (!initialize || !shutdown || !count || !handle || !pci || initialize() != Nvml::Success) {
                return;
            }
            initialized = true;
            usage = Resolve<Nvml::Usage>(nvml, "nvmlDeviceGetUtilizationRates");
            temperature = Resolve<Nvml::Temperature>(nvml, "nvmlDeviceGetTemperature");
            power = Resolve<Nvml::Power>(nvml, "nvmlDeviceGetPowerUsage");
            clock = Resolve<Nvml::Clock>(nvml, "nvmlDeviceGetClockInfo");
            memory = Resolve<Nvml::MemoryInfo>(nvml, "nvmlDeviceGetMemoryInfo");
            unsigned int devices = 0;
            if (count(&devices) != Nvml::Success || devices > MaxAdapters) {
                return;
            }
            for (auto& adapter : adapters) {
                if (adapter.description.VendorId != 0x10DE) {
                    continue;
                }
                const auto address = PciAddress(adapter.description.AdapterLuid);
                if (!address) {
                    continue;
                }
                unsigned int matches = 0;
                for (unsigned int i = 0; i < devices; ++i) {
                    Nvml::Device device = nullptr;
                    Nvml::PciInfo info{};
                    unsigned int domain = 0, bus = 0, slot = 0, function = 0;
                    if (handle(i, &device) == Nvml::Success && pci(device, &info) == Nvml::Success &&
                        sscanf_s(info.busId, "%x:%x:%x.%x", &domain, &bus, &slot, &function) == 4 &&
                        domain == 0 && bus == address->BusNumber && slot == address->DeviceNumber &&
                        function == address->FunctionNumber) {
                        adapter.nvml = device;
                        ++matches;
                    }
                }
                if (matches != 1) {
                    adapter.nvml = nullptr;
                }
            }
        }

        void ReadNvml(Nvml::Device device, GpuReading& reading) const
        {
            if (!device) {
                return;
            }
            unsigned int value = 0;
            Nvml::Utilization utilization{};
            if (usage && usage(device, &utilization) == Nvml::Success && utilization.gpu <= 100) {
                reading.usage = utilization.gpu;
            }
            if (temperature && temperature(device, 0, &value) == Nvml::Success) {
                reading.temperature = value;
            }
            if (power && power(device, &value) == Nvml::Success) {
                reading.watts = value / 1000.0;
            }
            if (clock && clock(device, 0, &value) == Nvml::Success) {
                reading.clockMHz = value;
            }
            Nvml::Memory values{};
            if (memory && memory(device, &values) == Nvml::Success) {
                reading.boardMemoryGiB = values.used / GiB;
                reading.boardTotalGiB = values.total / GiB;
            }
        }

        std::vector<GpuReading> Poll()
        {
            const bool collected = query && PdhCollectQueryData(query) == ERROR_SUCCESS;
            const auto engineSamples = collected ? ReadCounter(engines) : std::vector<CounterSample>{};
            const auto memorySamples = collected ? ReadCounter(dedicated) : std::vector<CounterSample>{};
            std::vector<GpuReading> result;
            for (const auto& adapter : adapters) {
                GpuReading reading;
                reading.name = Utf8(adapter.description.Description);
                if (adapter.description.DedicatedVideoMemory != 0) {
                    reading.boardTotalGiB = adapter.description.DedicatedVideoMemory / GiB;
                }
                DXGI_QUERY_VIDEO_MEMORY_INFO memoryInfo{};
                if (SUCCEEDED(adapter.dxgi->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &memoryInfo))) {
                    reading.gameMemoryGiB = memoryInfo.CurrentUsage / GiB;
                    reading.gameBudgetGiB = memoryInfo.Budget / GiB;
                }
                std::map<std::wstring, double> engineTotals;
                for (const auto& sample : engineSamples) {
                    const auto position = sample.name.find(adapter.counterLuid);
                    if (position != std::wstring::npos) {
                        // Strip PID, retaining physical adapter and engine identity.
                        engineTotals[sample.name.substr(position)] += sample.value;
                    }
                }
                for (const auto& [engine, total] : engineTotals) {
                    reading.usage = std::max(reading.usage.value_or(0), std::clamp(total, 0.0, 100.0));
                }
                for (const auto& sample : memorySamples) {
                    if (sample.name.starts_with(adapter.counterLuid)) {
                        reading.boardMemoryGiB = reading.boardMemoryGiB.value_or(0) + sample.value / GiB;
                    }
                }
                ReadNvml(adapter.nvml, reading);
                result.push_back(std::move(reading));
            }
            return result;
        }
    };

    GpuCollector::GpuCollector() : impl_(std::make_unique<Impl>()) {}
    GpuCollector::~GpuCollector() = default;
    std::vector<GpuReading> GpuCollector::Poll() { return impl_->Poll(); }
}
