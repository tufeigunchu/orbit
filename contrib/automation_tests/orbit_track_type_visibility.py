"""
Copyright (c) 2021 The Orbit Authors. All rights reserved.
Use of this source code is governed by a BSD-style license that can be
found in the LICENSE file.
"""

from absl import app

from core.orbit_e2e import E2ETestSuite
from test_cases.connection_window import FilterAndSelectFirstProcess, ConnectToStadiaInstance
from test_cases.capture_window import Capture, VerifyTracksExist, ToggleTrackTypeVisibility, VerifyTracksDoNotExist
"""Toggle track type visibility in Orbit using pywinauto.

Before this script is run there needs to be a gamelet reserved and
"hello_ggp_standalone" has to be started.

The script requires absl and pywinauto. Since pywinauto requires the bitness of
the python installation to match the bitness of the program under test it needs
to by run from  64 bit python.

This automation script covers a basic workflow:
 - start Orbit
 - connect to a gamelet
 - take a capture
 - open the track configuration pane
 - hide the "Scheduler" track by type and verify it's no longer visible
 - hide all "Thread" tracks and verify they are gone
 - restore the tracks and verify they are back again
"""


def main(argv):
    test_cases = [
        ConnectToStadiaInstance(),
        FilterAndSelectFirstProcess(process_filter='hello_ggp'),
        Capture(),
        ToggleTrackTypeVisibility(track_type="Scheduler"),
        VerifyTracksDoNotExist(track_names="Scheduler"),
        VerifyTracksExist(track_names="hello_ggp_stand*", allow_duplicates=True),
        ToggleTrackTypeVisibility(track_type="Threads"),
        VerifyTracksDoNotExist(track_names="hello_ggp_stand*"),
        ToggleTrackTypeVisibility(track_type="Threads"),
        VerifyTracksExist(track_names="hello_ggp_stand*", allow_duplicates=True)
    ]
    suite = E2ETestSuite(test_name="Toggle track type visibility", test_cases=test_cases)
    suite.execute()


if __name__ == '__main__':
    app.run(main)
