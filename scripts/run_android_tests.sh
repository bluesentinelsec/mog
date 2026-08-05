#!/usr/bin/env bash
set -euo pipefail

apk="${1:-android/test-app/build/outputs/apk/release/test-app-release.apk}"
package="com.bluesentinelsec.mog.test"
activity="${package}/.TestActivity"
timeout_seconds="${MOG_ANDROID_TEST_TIMEOUT:-120}"

if [[ ! -f "${apk}" ]]; then
  echo "Android test APK not found: ${apk}" >&2
  exit 1
fi

adb install -r "${apk}" >/dev/null
adb logcat -c
adb shell am force-stop "${package}"
adb shell am start -W -n "${activity}" >/dev/null

deadline=$((SECONDS + timeout_seconds))
while (( SECONDS < deadline )); do
  logs="$(adb logcat -d -s mog-android-test:I '*:S')"
  if grep -q "MOG_ANDROID_TESTS: PASS" <<<"${logs}"; then
    printf '%s\n' "${logs}"
    exit 0
  fi
  if grep -q "MOG_ANDROID_TESTS: FAIL" <<<"${logs}"; then
    printf '%s\n' "${logs}" >&2
    exit 1
  fi
  sleep 1
done

adb logcat -d -s mog-android-test:I '*:S' >&2
echo "Timed out after ${timeout_seconds}s waiting for Android tests" >&2
exit 1
