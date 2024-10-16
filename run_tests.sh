#! /usr/bin/env bash

# To run all tests:
#   ./run_tests.sh 
# To see the options available:
#   ./run_tests.sh -h

# Color output can be disable by using the -n opt.
RED='[0;31m'
GREEN='[0;32m'
YELLOW='[0;33m'
BLUE='[0;34m'
BOLD='[1m'
NONE='[0;00m'

# Note: testing script runs the server in the background and then kills it + all nodes using pkill,
# which will kill all processes with the same name as the server executable, so maybe make sure
# you don't have other things named the same thing as PROGRAM_NAME
TESTDIR="./tests"
OUTPUTDIR="./output"
EXPECTEDDIR="./tests/expected"
INPUTSDIR="./tests/inputs"
PROGRAM_NAME="controller"

# Run make clean and then make all.
run_make () {
  local verbose=$1
  local moutput=""
  local makeargs="LOG=1"
  make clean > /dev/null 2>&1
  printf "${GREEN}Running ${BOLD}%-24s${NONE} " "make all:"
  moutput=$(make ${makeargs} all 2>&1)
  if [ $? != 0 ]; then 
    printf "${RED}failed!${NONE}\n"
    echo "${moutput}"
    exit 1
  else 
    printf "${BLUE}ok!${NONE}\n"
  fi
  if [ ${verbose} -eq 1 ]; then 
    echo "${moutput}"
  fi
  return 
}

# If test fails due to differences in expected and actual output, print names
# of files it is using for comparison purposes.
print_diff_info () {
  local actual_output=$1
  local expected_output=$2
  printf "    You can compare the results using 'diff' or 'cmp'\n" 
  printf "    - %s\n" "${BOLD}Actual   output${NONE}: ${actual_output}"
  printf "    - %s\n" "${BOLD}Expected output${NONE}: ${expected_output}"
  return
}

run_test () {
  local testname=$1
  local verbose=$2
  local diff_ret=0 # return code from diff
  local test_file="${TESTDIR}/${testname}.args"
  local passed=1

  mkdir -p $OUTPUTDIR/$testname
  local OUTPUTDIR=$OUTPUTDIR/$testname

  printf "${BLUE}Running test ${BOLD}%-19s${NONE} " "${testname}:"

  # -------------------- Parse test file ---------------------

  # Key for test files:
  # -t input1,input2,...   list of input files containing requests to be sent to the controller
  # -c                     specifies to send each request file's requests concurrently (default is sequential)
  # -e expected            path to file with expected result
  # -- ...                 arguments after the '--' are used as the args of the controller

  local num_nodes=0
  local req_files=()
  local concurrent=0
  local port_num="1024" # default port is 1024
  local expected=""
  local controller_args=""

  local args=`cat $test_file`

  options=`getopt t:p:ce: $args`
  errcode=$?
  if [ ${errcode} -ne 0 ]; then 
    echo "illegal test configuration; aborting"
    exit 1
  fi

  set -- $options
  for i; do
    case "$i" in
      -t) 
        IFS=',' read -r -a terms <<< "$2"
        for i in `seq 0 $((${#terms[@]}-1))`; do 
          req_files[$i]=$INPUTSDIR/${terms[i]}
        done
        shift;
        shift;;
      -p) port_num=$2; shift; shift;;
      -c) concurrent=1; shift;;
      -e) expected=$EXPECTEDDIR/$2; shift; shift;;
      --)
        shift;
        controller_args="$*"
        shift; break;;
    esac
  done

  controller_args="-p ${port_num} ${controller_args}"

  if [[ ${#req_files[@]} -eq 0 ]]; then
    echo "Illegal test configuration; aborting"
    exit
  fi

  # ----------------------------- Start the server -----------------------------

  local server_out=$OUTPUTDIR/server_out

  touch ${server_out}

  ./$PROGRAM_NAME $controller_args > $server_out 2>&1 &
  local server_pid=$!

  # ------------------------------ Send requests -------------------------------

  local pids=()
  local req_err=0 # whether an error occurred while requesting
  local req_ret=0 # return code of req_ret
  local tmr_err=0 # if a timeout occured.

  for i in `seq 0 $((${#req_files[@]}-1))`; do
    if [ $concurrent -eq 1 ]; then
      ${TIMEOUT} ./send_requests.sh ${req_files[i]} ${port_num} $OUTPUTDIR/response$i &
      pids[i]=$!
      req_ret="$?"
      if [ "${req_ret}" == "124" ]; then # timed out
        tmr_err=1
      elif [ "${req_ret}" != "0" ]; then 
        req_err=1
      fi
    else
      ${TIMEOUT} ./send_requests.sh ${req_files[i]} ${port_num} $OUTPUTDIR/response$i
      req_ret="$?"
      if [ "${req_ret}" == "124" ]; then # timed out
        tmr_err=1
      elif [ "${req_ret}" != "0" ]; then 
        req_err=1
      fi
    fi
  done

  if [ $concurrent -eq 1 ]; then 
    for pid in ${pids[@]}; do
      wait $pid
      req_ret="$?"
      if [ "${req_ret}" == "124" ]; then 
        tmr_err=1
      elif [ "${req_ret}" != "0" ]; then 
        req_err=1
      fi
    done
  fi

  # ---------------------- Concatenate Responses Together ----------------------
  touch $OUTPUTDIR/response
  for i in `seq 0 $((${#req_files[@]}-1))`; do
    cat $OUTPUTDIR/response$i >> $OUTPUTDIR/response
  done

  output=$OUTPUTDIR/response

  # ----------------------- Check Responses From Server ------------------------

  if [ $tmr_err -eq 1 ]; then 
    passed=0
    printf "${RED}failed!${NONE}\n"
    printf "  - %s\n" "One or more of the requests sent to the server timed out"
  fi

  # Check return code indicates all requests sent correctly
  # Even if this fails, it will still run the diff step
  if [ $req_err -eq 1 ]; then 
    passed=0
    printf "${RED}failed!${NONE}\n"
    printf "  - %s\n" "One or more of the requests sent to the server failed"
  fi

  # Compare expected output with actual output
  if [ "${expected}" != "" ]; then 
    diff_output=$(diff ${output} ${expected})
    diff_ret=$?
    if [ ${diff_ret} -ne 0 ]; then
      if [ ${passed} -ne 0 ]; then printf "${RED}failed!${NONE}\n"; fi
      passed=0
      printf "  - %s\n" "Actual output does not match expected output"
      print_diff_info $output $expected
    fi 
  fi

  if [ $passed -eq 1 ]; then 
    printf "${GREEN}passed!${NONE}\n"
  else 
    # If test doesn't pass, print out what was run
    printf "  - %s\n" "${BOLD}What is being run${NONE}:  ./${PROGRAM_NAME} ${controller_args}"
    printf "  - %s\n" "${BOLD}View stdout/stderr${NONE}: ${server_out}"
  fi 

  # End the server

  pkill -x -f "./$PROGRAM_NAME $controller_args" >/dev/null 2>&1
  wait ${server_pid} >/dev/null 2>&1

  # If verbose option set, print out the contents of stdout + stderr
  if [ "$verbose" == "1" ]; then
    printf "%s\n" " ============================ Output ============================ "
    cat ${server_out}
    printf "\n"
    printf "%s\n" " ================================================================ "
  fi

  return $passed
} 

# Print usage information
usage () {
  echo "usage: ./run_tests.sh [-h] [-v] [-n] [-t test]"
  echo "  -h                     (help)     show this help message and exit."
  echo "  -n                     (nocolor)  disable color output when printing test results."
  echo "  -t test                (test)     specify the test to run"
  echo "  -v                     (verbose)  print stdout and stderr when running tests. enables LOG macro in server as well."
  return 0
}

verbose=0
color=1
test=""

while getopts 'hnvt:' opt; do
  case "$opt" in
    n)
      RED=
      GREEN=
      YELLOW=
      BLUE=
      BOLD=
      UNDERLINE=
      NONE=
      ;;
    t) 
      test=$OPTARG
      ;;
    v) 
      verbose=1
      ;;
    ?|h)
      usage
      exit 0;;
  esac
done

if [ "${test}" != "" ]; then
  ALL_TESTS=${test}
else 
  BASIC_TESTS="basic-1 basic-2 basic-3 basic-4 basic-5 basic-6"
  MULTI_TESTS="multi-1 multi-2"
  CONC_TESTS="concurrent-1 concurrent-2 concurrent-3"
  ALL_TESTS="${BASIC_TESTS} ${MULTI_TESTS} ${CONC_TESTS}"
fi

# Timeout
TIMEOUT=$(which timeout)
if [ "$?" = "0" ]; then
    TIMEOUT="timeout 10"
else
    printf "%s\n" "${YELLOW}${BOLD}Warning - could not find the 'timeout' command${NONE}"
    TIMEOUT=""
fi

run_make $verbose
# Just run this once instead of a bunch of times for each file
rm -rf ${OUTPUTDIR}
mkdir -p ${OUTPUTDIR}

# TOTAL_PASSED=0

(( tests_run = 0    ))
(( tests_passed = 0 ))
for t in $ALL_TESTS; do
  run_test $t $verbose
  x=$?
  (( tests_run = $tests_run + 1 ))
  (( tests_passed = $tests_passed + $x ))
done

printf "%s\n" "${BOLD}Total${NONE}: ${tests_passed}/${tests_run} tests passed."

