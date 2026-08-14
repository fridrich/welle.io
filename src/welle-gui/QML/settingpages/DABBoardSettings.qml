/*
 *    Copyright (C) 2026
 *    Silicon Labs Si4688 DABBoard Settings Page
 */

import QtQuick
import QtQuick.Layouts

// Import custom styles
import "../texts"
import "../components"

Item {
    function initDevice(isAutoDevice) {
        if(!isAutoDevice)
            guiHelper.openDabboard()
    }
}
