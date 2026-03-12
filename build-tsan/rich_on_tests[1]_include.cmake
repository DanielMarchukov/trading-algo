if(EXISTS "/home/danmarchukov/repos/rich-on-paper/build-tsan/rich_on_tests")
  if(NOT EXISTS "/home/danmarchukov/repos/rich-on-paper/build-tsan/rich_on_tests[1]_tests.cmake" OR
     NOT "/home/danmarchukov/repos/rich-on-paper/build-tsan/rich_on_tests[1]_tests.cmake" IS_NEWER_THAN "/home/danmarchukov/repos/rich-on-paper/build-tsan/rich_on_tests" OR
     NOT "/home/danmarchukov/repos/rich-on-paper/build-tsan/rich_on_tests[1]_tests.cmake" IS_NEWER_THAN "${CMAKE_CURRENT_LIST_FILE}")
    include("/usr/share/cmake-3.28/Modules/GoogleTestAddTests.cmake")
    gtest_discover_tests_impl(
      TEST_EXECUTABLE [==[/home/danmarchukov/repos/rich-on-paper/build-tsan/rich_on_tests]==]
      TEST_EXECUTOR [==[]==]
      TEST_WORKING_DIR [==[/home/danmarchukov/repos/rich-on-paper/build-tsan]==]
      TEST_EXTRA_ARGS [==[]==]
      TEST_PROPERTIES [==[]==]
      TEST_PREFIX [==[]==]
      TEST_SUFFIX [==[]==]
      TEST_FILTER [==[]==]
      NO_PRETTY_TYPES [==[FALSE]==]
      NO_PRETTY_VALUES [==[FALSE]==]
      TEST_LIST [==[rich_on_tests_TESTS]==]
      CTEST_FILE [==[/home/danmarchukov/repos/rich-on-paper/build-tsan/rich_on_tests[1]_tests.cmake]==]
      TEST_DISCOVERY_TIMEOUT [==[5]==]
      TEST_XML_OUTPUT_DIR [==[]==]
    )
  endif()
  include("/home/danmarchukov/repos/rich-on-paper/build-tsan/rich_on_tests[1]_tests.cmake")
else()
  add_test(rich_on_tests_NOT_BUILT rich_on_tests_NOT_BUILT)
endif()
