# SPDX-License-Identifier: MIT
#
# Drives the console example with a scripted session on stdin and compares its
# stdout with the recorded transcript.  The session carries trigger arguments,
# so it also covers guards deciding between alternatives.  Refused triggers and
# unknown channels are checked on stderr.
#
# Run through ctest; see the car_console_pipe test in the top-level CMakeLists.

execute_process(
  COMMAND         "${EXE}" "${SETUP}" "${MACHINE}" --quiet
  INPUT_FILE      "${SCRIPT}"
  OUTPUT_VARIABLE actual
  ERROR_VARIABLE  errors
  RESULT_VARIABLE code)

if(NOT code EQUAL 0)
  message(FATAL_ERROR "car_console exited with ${code}\nstderr:\n${errors}")
endif()

file(READ "${EXPECTED}" expected)
string(REPLACE "\r\n" "\n" actual "${actual}")
string(REPLACE "\r\n" "\n" expected "${expected}")

if(NOT actual STREQUAL expected)
  message(FATAL_ERROR
    "stdout does not match the transcript\n--- expected ---\n${expected}"
    "--- actual ---\n${actual}--- stderr ---\n${errors}")
endif()

foreach(needle
    "rejected: brake_released in state power_off"
    "rejected: vehicle_stopped in state braking: no guard matched .speed=20."
    "unknown channel: handbrake")
  if(NOT errors MATCHES "${needle}")
    message(FATAL_ERROR "stderr is missing '${needle}'\n--- stderr ---\n${errors}")
  endif()
endforeach()

message(STATUS "console session matched the transcript")
