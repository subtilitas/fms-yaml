# SPDX-License-Identifier: MIT
#
# Drives the console example with a scripted session on stdin and compares its
# stdout with the recorded transcript.  Also checks that the two refused
# triggers and the unknown channel were reported on stderr.
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
    "rejected: brake_pressed in state self_test"
    "rejected: vehicle_stopped in state accelerating"
    "unknown channel: handbrake")
  if(NOT errors MATCHES "${needle}")
    message(FATAL_ERROR "stderr is missing '${needle}'\n--- stderr ---\n${errors}")
  endif()
endforeach()

message(STATUS "console session matched the transcript")
