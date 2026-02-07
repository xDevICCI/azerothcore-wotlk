/*
 * This file is part of the AzerothCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "Config.h"
#include "Log.h"
#include "ScriptMgr.h"
#include "World.h"

#include "CompilerDefs.h"
#include <cstdint>

#if AC_PLATFORM == AC_PLATFORM_WINDOWS
#include <windows.h>
#include <psapi.h>
#elif AC_PLATFORM == AC_PLATFORM_APPLE
#include <mach/mach.h>
#else // AC_PLATFORM_UNIX
#include <cstdio>
#include <cstring>
#include <cstdlib>
#endif

namespace
{
    uint32_t GetProcessMemoryUsageMB()
    {
#if AC_PLATFORM == AC_PLATFORM_WINDOWS
        PROCESS_MEMORY_COUNTERS pmc;
        if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
            return static_cast<uint32_t>(pmc.WorkingSetSize / (1024 * 1024));
        return 0;
#elif AC_PLATFORM == AC_PLATFORM_APPLE
        struct mach_task_basic_info info;
        mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
        if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                      reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS)
            return static_cast<uint32_t>(info.resident_size / (1024 * 1024));
        return 0;
#else // AC_PLATFORM_UNIX (Linux)
        FILE* file = fopen("/proc/self/status", "r");
        if (!file)
            return 0;

        uint32_t memKB = 0;
        char line[256];
        while (fgets(line, sizeof(line), file))
        {
            if (strncmp(line, "VmRSS:", 6) == 0)
            {
                char* ptr = line + 6;
                while (*ptr == ' ' || *ptr == '\t')
                    ++ptr;
                memKB = static_cast<uint32_t>(atol(ptr));
                break;
            }
        }
        fclose(file);
        return memKB / 1024;
#endif
    }

    bool _enabled = false;
    uint32 _maxMB = 0;
    uint32 _warnMB = 0;
    uint32 _checkIntervalMs = 30000;
    uint32 _timer = 0;
    bool _shutdownTriggered = false;

    void LoadConfig()
    {
        _enabled = sConfigMgr->GetOption<bool>("MemoryCap.Enable", false);
        _maxMB = sConfigMgr->GetOption<uint32>("MemoryCap.MaxMB", 0);
        _warnMB = sConfigMgr->GetOption<uint32>("MemoryCap.WarnMB", 0);

        uint32 interval = sConfigMgr->GetOption<uint32>("MemoryCap.CheckInterval", 30);
        if (interval < 5)
            interval = 5;
        _checkIntervalMs = interval * 1000;

        _timer = _checkIntervalMs;
        _shutdownTriggered = false;

        if (_enabled)
        {
            LOG_INFO("server.loading", ">> mod-memory-cap: Enabled (Max: {}MB, Warn: {}MB, Interval: {}s)",
                _maxMB, _warnMB, interval);
        }
    }
}

class MemoryCapWorldScript : public WorldScript
{
public:
    MemoryCapWorldScript() : WorldScript("MemoryCapWorldScript") { }

    void OnAfterConfigLoad(bool /*reload*/) override
    {
        LoadConfig();
    }

    void OnUpdate(uint32 diff) override
    {
        if (!_enabled)
            return;

        if (_timer <= diff)
        {
            _timer = _checkIntervalMs;

            uint32 currentMemMB = GetProcessMemoryUsageMB();

            if (_maxMB > 0 && currentMemMB >= _maxMB && !_shutdownTriggered)
            {
                _shutdownTriggered = true;
                LOG_ERROR("server", "mod-memory-cap: Memory usage {}MB exceeds cap {}MB. Initiating restart...",
                    currentMemMB, _maxMB);
                sWorld->ShutdownServ(30, SHUTDOWN_MASK_RESTART, RESTART_EXIT_CODE,
                    "Memory cap exceeded (" + std::to_string(currentMemMB) + "MB/" +
                    std::to_string(_maxMB) + "MB)");
            }
            else if (_warnMB > 0 && currentMemMB >= _warnMB)
            {
                LOG_WARN("server", "mod-memory-cap: Memory usage warning: {}MB (warn: {}MB, cap: {}MB)",
                    currentMemMB, _warnMB, _maxMB > 0 ? _maxMB : 0);
            }
        }
        else
        {
            _timer -= diff;
        }
    }
};

void AddSC_mod_memory_cap()
{
    new MemoryCapWorldScript();
}
