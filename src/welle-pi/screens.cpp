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
#include "screens.hpp"
#include "ui_manager.hpp"
#include "utils.hpp"
#include <thread>

// ScanningScreen implementation
ScanningScreen::ScanningScreen() : m_channel(""), m_found(0), m_interrupted(false), m_changed(true) {}

void ScanningScreen::setScanningStatus(const std::string& channel, int foundCount) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_channel = channel;
    m_found = foundCount;
    m_changed = true;
}

void ScanningScreen::draw(IDisplay& display) {
    m_interrupted = false;
    m_changed = true; // Force redraw on screen transition
    while (!m_interrupted) {
        bool expected = true;
        if (m_changed.compare_exchange_strong(expected, false)) {
            std::string channel;
            int found;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                channel = m_channel;
                found = m_found;
            }
            display.clear();
            display.gotoXY(0,0);
            std::string line1 = "Tuning: " + channel + "...";
            display.write(line1.c_str());
            display.killEOL();
            display.gotoXY(0,1);
            std::string line2 = "Found: " + std::to_string(found);
            display.write(line2.c_str());
            display.killEOL();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void ScanningScreen::interrupt() {
    m_interrupted = true;
}

void ScanningScreen::handleInput(InputAction action) {
    (void)action;
}

// RadioScreen implementation
RadioScreen::RadioScreen() : m_channelName(""), m_programName(""), m_interrupted(false), m_changed(true), m_signalPresent(true), m_signalChanged(now_ms()) {}

void RadioScreen::setChannelName(const std::string& channel_name) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_channelName != channel_name) {
        m_channelName = channel_name;
        m_changed = true;
    }
}

void RadioScreen::setProgramName(const std::string& program_name, const std::string& short_program) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_programName != program_name || m_shortProgramName != short_program) {
        m_programName = program_name;
        m_shortProgramName = short_program;
        m_changed = true;
        m_dlsQueue.clear();
        m_dls.clear();
    }
}

void RadioScreen::setDLS(const std::string& dls) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_dlsQueue.empty())
        m_changed = true;
    if (m_dlsQueue.empty() || m_dlsQueue.back() != dls) {
        m_dlsQueue.push_back(dls);
    }
}

void RadioScreen::setSignalPresent(bool present) {
    if (m_signalPresent.exchange(present) != present) {
        m_signalChanged = now_ms();
        m_changed = true;
    }
}

bool RadioScreen::signalLost() const {
    return !m_signalPresent && (now_ms() - m_signalChanged > SIGNAL_LOST_MS);
}

void RadioScreen::draw(IDisplay& display) {
    m_interrupted = false;
    m_changed = true; // Force redraw on screen transition
    while (!m_interrupted) {
        bool expected = true;
        if (m_changed.compare_exchange_strong(expected, false)) {
            std::string programName, channelName, shortProgramName;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                programName = m_programName;
                shortProgramName = m_shortProgramName;
                channelName = m_channelName;
            }
            display.clear();
            display.gotoXY(0,0);
            
            std::string pToDraw = programName;
            if (display.getWidth() && pToDraw.length() > (size_t)display.getWidth() && !shortProgramName.empty()) {
                pToDraw = shortProgramName;
            }

            display.write(pToDraw.c_str());
            display.killEOL();
            display.gotoXY(0,1);
            display.write(channelName.c_str());
            display.killEOL();
            display.gotoXY(0,0);
            display.gotoLastLine();

            while (!m_changed && !m_interrupted) {
                const bool lost = signalLost();
                std::string text;
                bool queueEmpty;
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    if (!m_dlsQueue.empty()) {
                        m_dls = m_dlsQueue.front();
                        m_dlsQueue.pop_front();
                    }
                    queueEmpty = m_dlsQueue.empty();
                    text = lost ? NO_SIGNAL_TEXT : m_dls;
                }
                if (display.scroll(text.c_str())) {
                } else {
                    int wait_steps = 10;
                    while (!m_changed && !m_interrupted && queueEmpty && lost == signalLost() && wait_steps > 0) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                        wait_steps--;
                    }
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void RadioScreen::interrupt() {
    m_interrupted = true;
}

void RadioScreen::handleInput(InputAction action) {
    if (m_inputCallback) {
        m_inputCallback(action);
    }
}

void RadioScreen::setInputCallback(std::function<void(InputAction)> cb) {
    m_inputCallback = cb;
}

// StationListScreen implementation
StationListScreen::StationListScreen(const std::vector<ConfigManager::Station>& stations, UIManager* uiManager)
    : m_stations(stations), m_uiManager(uiManager), m_index(0), m_interrupted(false), m_changed(true), m_selected(false) {}

void StationListScreen::draw(IDisplay& display) {
    m_interrupted = false;
    while (!m_interrupted) {
        bool expected = true;
        if (m_changed.compare_exchange_strong(expected, false)) {
            display.clear();
            
            int width = display.getWidth();
            if (width == 0) width = 40; // We are debugging and only the debug output is interesting
            if (width < 4) width = 16; // fallback safety
            int height = display.getHeight();
            if (height < 2) height = 2; // fallback safety
            
            if (m_stations.empty()) {
                display.gotoXY(0,0);
                display.write("No stations!");
                display.killEOL();
            } else {
                // Line 0: Header with index/total
                display.gotoXY(0,0);
                std::string header = "Select: " + std::to_string(m_index + 1) + "/" + std::to_string(m_stations.size());
                if (header.length() > (size_t)width) {
                    header = std::to_string(m_index + 1) + "/" + std::to_string(m_stations.size());
                }
                display.write(header.c_str());
                display.killEOL();
                
                // Determine where to place the focused line relative to display lines 1..height-1
                int list_lines = height - 1;
                int focused_line = list_lines / 2; // e.g. for height=4 (list_lines=3), focused_line=1 (the middle line)
                
                for (int y = 1; y < height; ++y) {
                    display.gotoXY(0, y);
                    
                    int offset = y - 1 - focused_line;
                    // Safe modulo wrap-around
                    int item_index = ((int)m_index + offset + (int)m_stations.size()) % (int)m_stations.size();
                    
                    std::string prog = m_stations[item_index].program;
                    
                    if (offset == 0) {
                        // This is the active/focused station
                        if (width < 16) {
                            if (!m_stations[item_index].short_program.empty()) {
                                prog = m_stations[item_index].short_program;
                            }
                            std::string wrapped = ">" + prog;
                            display.write(wrapped.c_str());
                        } else {
                            // Typical 16+ chars display. Use long name, bounded by ">" and "<"
                            int name_limit = width - 2;
                            if (prog.length() > (size_t)name_limit) {
                                prog = prog.substr(0, name_limit);
                            }
                            if (prog.length() < (size_t)name_limit) {
                                prog.append(name_limit - prog.length(), ' ');
                            }
                            std::string wrapped = ">" + prog + "<";
                            display.write(wrapped.c_str());
                        }
                    } else {
                        // Non-focused station: raw unpadded string
                        display.write(prog.c_str());
                    }
                    display.killEOL();
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void StationListScreen::interrupt() {
    m_interrupted = true;
}

void StationListScreen::handleInput(InputAction action) {
    if (m_stations.empty()) return;
    if (action == InputAction::UP) {
        if (m_index == 0) m_index = m_stations.size() - 1;
        else m_index--;
        m_changed = true;
    } else if (action == InputAction::DOWN) {
        if (m_index == m_stations.size() - 1) m_index = 0;
        else m_index++;
        m_changed = true;
    } else if (action == InputAction::ENTER) {
        m_selected = true;
        if (m_selectCallback) {
            m_selectCallback(m_stations[m_index]);
        }
    } else if (action == InputAction::LEFT || action == InputAction::MENU || action == InputAction::QUIT) {
        if (m_uiManager) {
            m_uiManager->popScreen();
        }
    }
}

void StationListScreen::setSelectCallback(std::function<void(const ConfigManager::Station&)> cb) {
    m_selectCallback = cb;
}

void StationListScreen::setIndex(size_t index) {
    if (index < m_stations.size()) {
        m_index = index;
        m_changed = true;
    }
}

bool StationListScreen::isSelected() const { return m_selected; }
void StationListScreen::resetSelected() { m_selected = false; }

// MenuScreen implementation
MenuScreen::MenuScreen(UIManager* uiManager) : m_uiManager(uiManager), m_index(0), m_interrupted(false), m_changed(true) {
    m_options.push_back("1. Manual Rescan");
    m_options.push_back("2. Back to Radio");
    m_options.push_back("3. Exit");
}

void MenuScreen::draw(IDisplay& display) {
    m_interrupted = false;
    m_changed = true; // Force redraw on screen transition
    while (!m_interrupted) {
        bool expected = true;
        if (m_changed.compare_exchange_strong(expected, false)) {
            display.clear();
            display.gotoXY(0,0);
            display.write("Settings Menu");
            display.killEOL();
            display.gotoXY(0,1);
            display.write(m_options[m_index].c_str());
            display.killEOL();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void MenuScreen::interrupt() {
    m_interrupted = true;
}

void MenuScreen::handleInput(InputAction action) {
    if (action == InputAction::UP) {
        if (m_index == 0) m_index = m_options.size() - 1;
        else m_index--;
        m_changed = true;
    } else if (action == InputAction::DOWN) {
        if (m_index == m_options.size() - 1) m_index = 0;
        else m_index++;
        m_changed = true;
    } else if (action == InputAction::ENTER) {
        if (m_index == 1) { // 2. Back to Radio
            if (m_uiManager) {
                m_uiManager->popScreen();
            }
        } else {
            if (m_selectCallback) {
                m_selectCallback(m_index);
            }
        }
    } else if (action == InputAction::MENU || action == InputAction::LEFT) {
        if (m_uiManager) {
            m_uiManager->popScreen();
        }
    }
}

void MenuScreen::setSelectCallback(std::function<void(int)> cb) {
    m_selectCallback = cb;
}
