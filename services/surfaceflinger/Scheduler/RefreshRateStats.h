/*
 * Copyright 2019 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <numeric>

#include "Scheduler/RefreshRateConfigs.h"
#include "Scheduler/SchedulerUtils.h"
#include "TimeStats/TimeStats.h"

#include "android-base/stringprintf.h"
#include "utils/Timers.h"

namespace android {
namespace scheduler {

/**
 * Class to encapsulate statistics about refresh rates that the display is using. When the power
 * mode is set to HWC_POWER_MODE_NORMAL, SF is switching between refresh rates that are stored in
 * the device's configs. Otherwise, we assume the HWC is running in power saving mode under the
 * hood (eg. the device is in DOZE, or screen off mode).
 */
class RefreshRateStats {
    static constexpr int64_t MS_PER_S = 1000;
    static constexpr int64_t MS_PER_MIN = 60 * MS_PER_S;
    static constexpr int64_t MS_PER_HOUR = 60 * MS_PER_MIN;
    static constexpr int64_t MS_PER_DAY = 24 * MS_PER_HOUR;

public:
    explicit RefreshRateStats(const std::shared_ptr<RefreshRateConfigs>& refreshRateConfigs,
                              const std::shared_ptr<TimeStats>& timeStats)
          : mRefreshRateConfigs(refreshRateConfigs),
            mTimeStats(timeStats),
            mPreviousRecordedTime(systemTime()) {}
    ~RefreshRateStats() = default;

    // Sets power mode. We only collect the information when the power mode is not
    // HWC_POWER_MODE_NORMAL. When power mode is HWC_POWER_MODE_NORMAL, we collect the stats based
    // on config mode.
    void setPowerMode(int mode) {
        if (mCurrentPowerMode == mode) {
            return;
        }
        // If power mode is normal, the time is going to be recorded under config modes.
        if (mode == HWC_POWER_MODE_NORMAL) {
            mCurrentPowerMode = mode;
            return;
        }
        flushTime();
        mCurrentPowerMode = mode;
    }

    // Sets config mode. If the mode has changed, it records how much time was spent in the previous
    // mode.
    void setConfigMode(int mode) {
        if (mCurrentConfigMode == mode) {
            return;
        }
        flushTime();
        mCurrentConfigMode = mode;
    }

    // Returns a map between human readable refresh rate and number of seconds the device spent in
    // that mode.
    std::unordered_map<std::string, int64_t> getTotalTimes() {
        // If the power mode is on, then we are probably switching between the config modes. If
        // it's not then the screen is probably off. Make sure to flush times before printing
        // them.
        flushTime();

        std::unordered_map<std::string, int64_t> totalTime;
        for (auto [type, config] : mRefreshRateConfigs->getRefreshRates()) {
            int64_t totalTimeForConfig = 0;
            if (mConfigModesTotalTime.find(config.configId) != mConfigModesTotalTime.end()) {
                totalTimeForConfig = mConfigModesTotalTime.at(config.configId);
            }
            totalTime[config.name] = totalTimeForConfig;
        }
        return totalTime;
    }

    // Traverses through the map of config modes and returns how long they've been running in easy
    // to read format.
    std::string doDump() {
        std::ostringstream stream;
        stream << "+  Refresh rate: running time in seconds\n";
        for (auto stats : getTotalTimes()) {
            stream << stats.first.c_str() << ": " << getDateFormatFromMs(stats.second) << "\n";
        }
        return stream.str();
    }

private:
    void flushTime() {
        // Normal power mode is counted under different config modes.
        if (mCurrentPowerMode == HWC_POWER_MODE_NORMAL) {
            flushTimeForMode(mCurrentConfigMode);
        } else {
            flushTimeForMode(SCREEN_OFF_CONFIG_ID);
        }
    }

    // Calculates the time that passed in ms between the last time we recorded time and the time
    // this method was called.
    void flushTimeForMode(int mode) {
        nsecs_t currentTime = systemTime();
        nsecs_t timeElapsed = currentTime - mPreviousRecordedTime;
        int64_t timeElapsedMs = ns2ms(timeElapsed);
        mPreviousRecordedTime = currentTime;

        mConfigModesTotalTime[mode] += timeElapsedMs;
        for (const auto& [type, config] : mRefreshRateConfigs->getRefreshRates()) {
            if (config.configId == mode) {
                mTimeStats->recordRefreshRate(config.fps, timeElapsed);
            }
        }
    }

    // Formats the time in milliseconds into easy to read format.
    static std::string getDateFormatFromMs(int64_t timeMs) {
        auto [days, dayRemainderMs] = std::div(timeMs, MS_PER_DAY);
        auto [hours, hourRemainderMs] = std::div(dayRemainderMs, MS_PER_HOUR);
        auto [mins, minsRemainderMs] = std::div(hourRemainderMs, MS_PER_MIN);
        auto [sec, secRemainderMs] = std::div(minsRemainderMs, MS_PER_S);
        return base::StringPrintf("%" PRId64 "d%02" PRId64 ":%02" PRId64 ":%02" PRId64
                                  ".%03" PRId64,
                                  days, hours, mins, sec, secRemainderMs);
    }

    // Keeps information about refresh rate configs that device has.
    std::shared_ptr<RefreshRateConfigs> mRefreshRateConfigs;

    // Aggregate refresh rate statistics for telemetry.
    std::shared_ptr<TimeStats> mTimeStats;

    int64_t mCurrentConfigMode = SCREEN_OFF_CONFIG_ID;
    int32_t mCurrentPowerMode = HWC_POWER_MODE_OFF;

    std::unordered_map<int /* power mode */, int64_t /* duration in ms */> mConfigModesTotalTime;

    nsecs_t mPreviousRecordedTime;
};

} // namespace scheduler
} // namespace android
