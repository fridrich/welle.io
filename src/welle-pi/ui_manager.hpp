/*
 *    Copyright (C) 2026
 *    Fridrich Strba (fridrich.strba@bluewin.ch)
 *
 *    This file is part of the welle.io.
 *    Many of the ideas as implemented in welle.io are derived from
 *    other work, made available through the GNU general Public License.
 *    All copyrights of the original authors are recognized.
 *
 *    welle.io is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    welle.io is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with welle.io; if not, write to the Free Software
 *    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */
#ifndef UI_MANAGER_HPP
#define UI_MANAGER_HPP

#include "ui_screen.hpp"
#include <memory>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>

class UIManager {
public:
    UIManager();
    ~UIManager();

    void setScreen(std::shared_ptr<UIScreen> newScreen);
    void processInput(InputAction action);
    liblcd::LCDDisplay& getDisplay();

private:
    void run();

    liblcd::LCDDisplay m_display;
    std::shared_ptr<UIScreen> m_currentScreen;
    std::atomic<bool> m_exit;
    std::atomic<bool> m_screenChanged;
    std::mutex m_mutex;
    std::condition_variable m_cond;
    std::thread m_thread;
};

#endif // UI_MANAGER_HPP
