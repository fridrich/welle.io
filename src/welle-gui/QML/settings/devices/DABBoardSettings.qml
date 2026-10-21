/*
 *    Copyright (C) 2026
 *    Silicon Labs Si4688 DABBoard Settings Page
 */

import QtQuick
import QtQuick.Layouts

import "../../controls"

Item {
    function initDevice(isAutoDevice) {
        if(!isAutoDevice)
            guiHelper.openDabboard()
    }
}
