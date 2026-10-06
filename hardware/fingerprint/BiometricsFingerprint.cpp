/*
 * Copyright (C) 2017 The Android Open Source Project
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
#define LOG_TAG "fingerprint@2.3-service.tundra"

#include "BiometricsFingerprint.h"

#include <android-base/file.h>
#include <android-base/logging.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/stat.h>

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <thread>

#define NOTIFY_FINGER_UP IMotFodEventType::FINGER_UP
#define NOTIFY_FINGER_DOWN IMotFodEventType::FINGER_DOWN

#define FOD_HBM_PATH "/sys/devices/platform/soc/soc:qcom,dsi-display-primary/fod_hbm"
#define BRIGHTNESS_PATH \
    "/sys/devices/platform/soc/ae00000.qcom,mdss_mdp/backlight/panel0-backlight/brightness"

#define DISPLAY_ON_POLL_MS 5
#define DISPLAY_ON_TIMEOUT_MS 1000
#define FOD_HBM_HOLD_MS 200

namespace android {
namespace hardware {
namespace biometrics {
namespace fingerprint {
namespace V2_3 {
namespace implementation {

void setFodHbm(bool status) {
    android::base::WriteStringToFile(status ? "1" : "0", FOD_HBM_PATH);
}

// Treat an unreadable node as on, so we never delay the screen-on path.
static bool isDisplayOn() {
    std::string value;
    if (!android::base::ReadFileToString(BRIGHTNESS_PATH, &value)) return true;
    return atoi(value.c_str()) > 0;
}

void BiometricsFingerprint::disableHighBrightFod() {
    std::lock_guard<std::mutex> lock(mSetHbmFodMutex);

    if (!hbmFodEnabled)
        return;

    mMotoFingerprint->sendFodEvent(NOTIFY_FINGER_UP, {},
                                   [](IMotFodEventResult, const hidl_vec<signed char> &) {});
    setFodHbm(false);

    hbmFodEnabled = false;
}

void BiometricsFingerprint::enableHighBrightFod() {
    std::lock_guard<std::mutex> lock(mSetHbmFodMutex);

    if (hbmFodEnabled)
        return;

    setFodHbm(true);
    mMotoFingerprint->sendFodEvent(NOTIFY_FINGER_DOWN, {},
                                   [](IMotFodEventResult, const hidl_vec<signed char> &) {});

    hbmFodEnabled = true;
}

BiometricsFingerprint::BiometricsFingerprint() {
    biometrics_2_1_service = IBiometricsFingerprint_2_1::getService();
    mMotoFingerprint = IMotoFingerPrint::getService();

    hbmFodEnabled = false;
    mFingerSeq = 0;
}

Return<uint64_t> BiometricsFingerprint::setNotify(
    const sp<IBiometricsFingerprintClientCallback> &clientCallback) {
    return biometrics_2_1_service->setNotify(clientCallback);
}

Return<uint64_t> BiometricsFingerprint::preEnroll() {
    return biometrics_2_1_service->preEnroll();
}

Return<RequestStatus> BiometricsFingerprint::enroll(const hidl_array<uint8_t, 69> &hat,
                                                    uint32_t gid, uint32_t timeoutSec) {
    return biometrics_2_1_service->enroll(hat, gid, timeoutSec);
}

Return<RequestStatus> BiometricsFingerprint::postEnroll() {
    return biometrics_2_1_service->postEnroll();
}

Return<uint64_t> BiometricsFingerprint::getAuthenticatorId() {
    return biometrics_2_1_service->getAuthenticatorId();
}

Return<RequestStatus> BiometricsFingerprint::cancel() {
    auto ret = biometrics_2_1_service->cancel();
    BiometricsFingerprint::onFingerUp();
    return ret;
}

Return<RequestStatus> BiometricsFingerprint::enumerate() {
    return biometrics_2_1_service->enumerate();
}

Return<RequestStatus> BiometricsFingerprint::remove(uint32_t gid, uint32_t fid) {
    return biometrics_2_1_service->remove(gid, fid);
}

Return<RequestStatus> BiometricsFingerprint::setActiveGroup(uint32_t gid,
                                                            const hidl_string &storePath) {
    return biometrics_2_1_service->setActiveGroup(gid, storePath);
}

Return<RequestStatus> BiometricsFingerprint::authenticate(uint64_t operationId, uint32_t gid) {
    auto ret = biometrics_2_1_service->authenticate(operationId, gid);
    // Keyguard may restart authentication while the panel wakes up for a screen off
    // touch, so don't cancel a pending finger down here.
    BiometricsFingerprint::disableHighBrightFod();
    return ret;
}

Return<bool> BiometricsFingerprint::isUdfps(uint32_t) {
    return true;
}

Return<void> BiometricsFingerprint::onFingerDown(uint32_t, uint32_t, float, float) {
    uint32_t seq = ++mFingerSeq;
    bool displayOn = isDisplayOn();

    if (displayOn) BiometricsFingerprint::enableHighBrightFod();

    std::thread([this, seq, displayOn]() {
        if (!displayOn) {
            // With screen off unlock, SystemUI reports the finger before the panel is
            // on, so HBM would be set on a dark panel. Wait for it to light up first.
            for (int waited = 0; !isDisplayOn() && waited < DISPLAY_ON_TIMEOUT_MS;
                 waited += DISPLAY_ON_POLL_MS) {
                std::this_thread::sleep_for(std::chrono::milliseconds(DISPLAY_ON_POLL_MS));
            }
            if (seq != mFingerSeq) return;
            BiometricsFingerprint::enableHighBrightFod();
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(FOD_HBM_HOLD_MS));
        if (seq == mFingerSeq) BiometricsFingerprint::disableHighBrightFod();
    }).detach();

    return Void();
}

Return<void> BiometricsFingerprint::onFingerUp() {
    ++mFingerSeq;
    BiometricsFingerprint::disableHighBrightFod();

    return Void();
}

}  // namespace implementation
}  // namespace V2_3
}  // namespace fingerprint
}  // namespace biometrics
}  // namespace hardware
}  // namespace android
