"""
Copyright (c) 2020 The Orbit Authors. All rights reserved.
Use of this source code is governed by a BSD-style license that can be
found in the LICENSE file.
"""

from absl import app

from core.orbit_e2e import E2ETestSuite
from test_cases.connection_window import FilterAndSelectFirstProcess, ConnectToStadiaInstance
from test_cases.capture_window import Capture
from test_cases.bottom_up_tab import VerifyHelloGgpBottomUpContents
"""Inspect the bottom-up view in Orbit using pywinauto.

Before this script is run there needs to be a gamelet reserved and
"hello_ggp_standalone" has to be started.

The script requires absl and pywinauto. Since pywinauto requires the bitness of
the python installation to match the bitness of the program under test it needs
to by run from 64 bit python.

This automation script covers a basic workflow:
 - start Orbit
 - connect to a gamelet
 - select a process
 - take a capture
 - verify that the bottom-up view contains at least 10 rows
 - verify that the first item is "ioctl"
 - verify that the first child of the first item starts with "drm"
"""


def main(argv):
    test_cases = [
        ConnectToStadiaInstance(),
        FilterAndSelectFirstProcess(process_filter='hello_ggp'),
        Capture(),
        VerifyHelloGgpBottomUpContents()
    ]
    suite = E2ETestSuite(test_name="Bottom-Up View", test_cases=test_cases)
    suite.execute()


if __name__ == '__main__':
    app.run(main)
