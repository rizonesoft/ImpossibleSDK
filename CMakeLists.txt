cmake_minimum_required(VERSION 4.0)
project(ImpossibleSDK LANGUAGES C)
include(cmake/ImpossibleSDK.cmake)

impossible_add_program(hello-console CONSOLE TIER 0 SOURCES samples/console/main.c)
impossible_add_program(hello-window WINDOW TIER 1 SOURCES samples/window/main.c)
impossible_add_program(selftest CONSOLE TIER 0 SOURCES tests/fixtures/selftest.c)
impossible_add_program(reopen WINDOW TIER 1 SOURCES tests/fixtures/reopen.c)
impossible_add_program(ui-tests CONSOLE TIER 0 SOURCES tests/fixtures/ui_tests.c)
impossible_add_program(image-tests CONSOLE TIER 0 SOURCES tests/fixtures/image_tests.c)

impossible_add_program(control-shots CONSOLE TIER 0 SOURCES tests/fixtures/control_shots.c)

# Breaks the rules on purpose, so the tests can see the check refuse it.
impossible_add_program(forbidden CONSOLE UNCHECKED SOURCES tests/fixtures/forbidden.c)
