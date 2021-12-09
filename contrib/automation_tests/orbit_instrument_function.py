"""
Copyright (c) 2020 The Orbit Authors. All rights reserved.
Use of this source code is governed by a BSD-style license that can be
found in the LICENSE file.
"""

from absl import app

from core.orbit_e2e import E2ETestSuite
from test_cases.connection_window import FilterAndSelectFirstProcess, ConnectToStadiaInstance
from test_cases.capture_window import Capture, CheckTimers, ExpandTrack
from test_cases.symbols_tab import LoadSymbols, FilterAndHookFunction
from test_cases.live_tab import VerifyFunctionCallCount
"""Instrument a single function in Orbit using pywinauto.

Before this script is run there needs to be a gamelet reserved and
"hello_ggp_standalone" has to be started.

The script requires absl and pywinauto. Since pywinauto requires the bitness of
the python installation to match the bitness of the program under test it needs
to by run from 64 bit python.

This automation script covers a basic workflow:
 - start Orbit
 - connect to a gamelet
 - select a process and load debug symbols
 - instrument a function
 - take a capture and verify the hooked function is recorded
"""


def main(argv):
    test_cases = [
        ConnectToStadiaInstance(),
        FilterAndSelectFirstProcess(process_filter='hello_'),
        LoadSymbols(module_search_string="hello_ggp"),
        Capture(),
        CheckTimers(track_name_filter='Scheduler'),
        ExpandTrack(expected_name="gfx"),
        CheckTimers(track_name_filter='gfx_submissions', recursive=True),
        CheckTimers(track_name_filter="All Threads", expect_exists=False),
        CheckTimers(track_name_filter="hello_ggp_stand", expect_exists=False),
        FilterAndHookFunction(function_search_string='DrawFrame'),
        Capture(),
        VerifyFunctionCallCount(function_name='DrawFrame', min_calls=30, max_calls=3000),
        CheckTimers(track_name_filter="All Threads", expect_exists=False),
        CheckTimers(track_name_filter="hello_ggp_stand")
    ]
    suite = E2ETestSuite(test_name="Instrument Function", test_cases=test_cases)
    suite.execute()


if __name__ == '__main__':
    app.run(main)
