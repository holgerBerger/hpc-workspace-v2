# CDash submission settings, read by ctest_start()/ctest_submit() in dashboard
# scripts (see cmake/dashboard.cmake) and by include(CTest).
#
# The project must exist on https://my.cdash.org with the same name.

set(CTEST_PROJECT_NAME "hpc-workspace-v2")
set(CTEST_NIGHTLY_START_TIME "01:00:00 UTC")

set(CTEST_SUBMIT_URL "https://my.cdash.org/submit.php?project=${CTEST_PROJECT_NAME}")
set(CTEST_DROP_SITE_CDASH TRUE)
