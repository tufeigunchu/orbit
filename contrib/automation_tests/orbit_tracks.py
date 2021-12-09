"""
Copyright (c) 2020 The Orbit Authors. All rights reserved.
Use of this source code is governed by a BSD-style license that can be
found in the LICENSE file.
"""

from absl import app

from core.orbit_e2e import E2ETestSuite
from test_cases.capture_window import SelectTrack, DeselectTrack, MoveTrack, FilterTracks, VerifyTracksExist, Capture
from test_cases.connection_window import FilterAndSelectFirstProcess, ConnectToStadiaInstance


def main(argv):
    test_cases = [
        ConnectToStadiaInstance(),
        FilterAndSelectFirstProcess(process_filter="hello_ggp"),
        Capture(),
        # "sdma0" is not present on the DevKits, instead there is "vce0", so this tests for "sdma0 or vce0"
        VerifyTracksExist(track_names=[
            "Scheduler", ("*sdma0*", "*vce0*"), "gfx", "All Threads", "hello_ggp_stand"
        ]),
        SelectTrack(track_index=4),
        DeselectTrack(),
        SelectTrack(track_index=0, expect_failure=True),  # Scheduler track cannot be selected
        MoveTrack(track_index=4, new_index=0),
        MoveTrack(track_index=0, new_index=3),
        MoveTrack(track_index=3, new_index=4),
        # TODO: The numbers below are very pessimistic, but it's not assured additional tracks like
        # GgpSwapChain, GgpVideoIpcRead etc are present - GgpSwapChain is missing on the DevKit, others
        # depend on the samples that have been taken
        FilterTracks(filter_string="hello", expected_track_count=2),
        FilterTracks(filter_string="Hello", expected_track_count=2)
    ]
    suite = E2ETestSuite(test_name="Track Interaction", test_cases=test_cases)
    suite.execute()


if __name__ == '__main__':
    app.run(main)
