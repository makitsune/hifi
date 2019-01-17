#!/usr/bin/env bash
set -xeuo pipefail
./gradlew -PHIFI_ANDROID_PRECOMPILED=${HIFI_ANDROID_PRECOMPILED} -PVERSION_CODE=${VERSION_CODE} -PRELEASE_NUMBER=${RELEASE_NUMBER} -PRELEASE_TYPE=${RELEASE_TYPE} setupDependencies
./gradlew -PHIFI_ANDROID_PRECOMPILED=${HIFI_ANDROID_PRECOMPILED} -PVERSION_CODE=${VERSION_CODE} -PRELEASE_NUMBER=${RELEASE_NUMBER} -PRELEASE_TYPE=${RELEASE_TYPE} app:${ANDROID_BUILD_TARGET}