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
#include "ui_manager.hpp"
#include "lcd_display_adapter.hpp"

UIManager::UIManager() : UIManager(std::make_unique<LCDDisplayAdapter>()) {}

UIManager::UIManager(std::unique_ptr<IDisplay> display)
    : m_display(std::move(display)), m_exit(false), m_screenChanged(false)
{
    m_display->backlightOn();
    m_thread = std::thread(&UIManager::run, this);
}

UIManager::~UIManager() {
    m_exit = true;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_currentScreen) {
            m_currentScreen->interrupt();
        }
    }
    m_display->interrupt();
    m_cond.notify_one();
    if (m_thread.joinable()) {
        m_thread.join();
    }
    m_display->clear();
}

void UIManager::setScreen(std::shared_ptr<UIScreen> newScreen) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_currentScreen) {
            m_currentScreen->interrupt();
        }
        m_currentScreen = newScreen;
        m_screenChanged = true;
    }
    m_display->interrupt();
    m_cond.notify_one();
}

void UIManager::processInput(InputAction action) {
    std::shared_ptr<UIScreen> screen;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        screen = m_currentScreen;
    }
    if (screen) {
        screen->handleInput(action);
    }
}

IDisplay& UIManager::getDisplay() { return *m_display; }

void UIManager::run() {
    while (!m_exit) {
        std::shared_ptr<UIScreen> screen;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cond.wait(lock, [this]() { return m_exit || m_screenChanged; });
            if (m_exit) break;
            screen = m_currentScreen;
            m_screenChanged = false;
        }
        if (screen) {
            screen->draw(*m_display);
        }
    }
}
