"""
Copyright (c) 2020 The Orbit Authors. All rights reserved.
Use of this source code is governed by a BSD-style license that can be
found in the LICENSE file.
"""

import logging
import time

from pywinauto.base_wrapper import BaseWrapper

from core.orbit_e2e import E2ETestCase, wait_for_condition


class MoveTab(E2ETestCase):
    """
    Move a tab from the right widget to the left, and back again. Verify the position after each move.
    """

    def right_click_move_context(self, item):
        item.click_input(button='right')
        context_menu = self.suite.application.window(best_match="TabBarContextMenu")
        self.find_control("MenuItem", name_contains="Move",
                          parent=context_menu).click_input(button='left')

    @staticmethod
    def _count_tabs(tab_control: BaseWrapper) -> int:
        return len(tab_control.children(control_type='TabItem'))

    def _execute(self, tab_title, tab_name):
        # Find tab and left and right tab bar
        tab_item = self.find_control("TabItem", tab_title)
        right_tab_bar = self.find_control("Tab",
                                          parent=self.find_control("Group", "RightTabWidget"),
                                          recurse=False)
        left_tab_bar = self.find_control("Tab",
                                         parent=self.find_control("Group", "MainTabWidget"),
                                         recurse=False)

        # Init tests
        left_tab_count = self._count_tabs(left_tab_bar)
        right_tab_count = self._count_tabs(right_tab_bar)

        tab_parent = tab_item.parent()
        self.expect_eq(tab_parent, right_tab_bar,
                       "%s tab is initialized in the right pane" % tab_title)

        # Move "Functions" tab to the left pane, check no. of tabs and if the tab is enabled
        logging.info('Moving tab to the left pane (current tab count: %d)', right_tab_count)
        self.right_click_move_context(tab_item)
        self.expect_eq(self._count_tabs(right_tab_bar), right_tab_count - 1,
                       "1 tab removed from right pane")
        self.expect_eq(self._count_tabs(left_tab_bar), left_tab_count + 1,
                       "1 tab added to the left pane")

        tab_item = self.find_control("TabItem", name=tab_title)
        self.expect_eq(tab_item.parent(), left_tab_bar, "Tab is parented under the left pane")
        self.expect_true(self.find_control("Group", name=tab_name).is_visible(), "Tab is visible")

        # Move back, check no. of tabs
        logging.info('Moving "%s" tab back to the right pane', tab_title)
        self.right_click_move_context(tab_item)
        self.expect_eq(self._count_tabs(right_tab_bar), right_tab_count,
                       "1 tab removed from left pane")
        self.expect_eq(self._count_tabs(left_tab_bar), left_tab_count,
                       "1 tab added to the right pane")

        tab_item = self.find_control("TabItem", name=tab_title)
        self.expect_eq(tab_item.parent(), right_tab_bar, "Tab is parented under the right pane")
        self.expect_true(
            self.find_control("Group", name=tab_name).is_visible(), "Functions tab is visible")


class EndSession(E2ETestCase):
    """
    Click menu entry to end session.
    """

    def _execute(self):
        time.sleep(1)
        app_menu = self.suite.top_window().descendants(control_type="MenuBar")[1]
        app_menu.item_by_path("File->End Session").click_input()
        wait_for_condition(
            lambda: self.suite.application.top_window().class_name() ==
            "orbit_session_setup::SessionSetupDialog", 30)
        self.suite.top_window(force_update=True)
