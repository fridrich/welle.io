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
#ifndef SCREENS_HPP
#define SCREENS_HPP

#include "ui_screen.hpp"
#include "config_manager.hpp"
#include <atomic>
#include <mutex>
#include <string>
#include <vector>
#include <deque>
#include <functional>

class ScanningScreen : public UIScreen {
public:
    ScanningScreen();
    void setScanningStatus(const std::string& channel, int foundCount);
    void draw(IDisplay& display) override;
    void interrupt() override;
    void handleInput(InputAction action) override;

private:
    std::mutex m_mutex;
    std::string m_channel;
    int m_found;
    std::atomic<bool> m_interrupted;
    std::atomic<bool> m_changed;
};

class RadioScreen : public UIScreen {
public:
    RadioScreen();
    void setChannelName(const std::string& channel_name);
    void setProgramName(const std::string& program_name, const std::string& short_program = "");
    void setDLS(const std::string& dls);
    void setSignalPresent(bool present);
    void draw(IDisplay& display) override;
    void interrupt() override;
    void handleInput(InputAction action) override;
    void setInputCallback(std::function<void(InputAction)> cb);

private:
    bool signalLost() const;

    static constexpr const char* NO_SIGNAL_TEXT = "-- no signal --";
    std::mutex m_mutex;
    std::string m_channelName;
    std::string m_programName;
    std::string m_shortProgramName;
    std::string m_dls;
    std::deque<std::string> m_dlsQueue;
    std::atomic<bool> m_interrupted;
    std::atomic<bool> m_changed;
    std::atomic<bool> m_signalPresent;
    std::atomic<uint64_t> m_signalChanged;
    std::function<void(InputAction)> m_inputCallback;
};

class UIManager;

class StationListScreen : public UIScreen {
public:
    StationListScreen(const std::vector<ConfigManager::Station>& stations, UIManager* uiManager);
    void draw(IDisplay& display) override;
    void interrupt() override;
    void handleInput(InputAction action) override;
    void setSelectCallback(std::function<void(const ConfigManager::Station&)> cb);
    void setIndex(size_t index);
    bool isSelected() const;
    void resetSelected();

private:
    std::vector<ConfigManager::Station> m_stations;
    UIManager* m_uiManager;
    size_t m_index;
    std::atomic<bool> m_interrupted;
    std::atomic<bool> m_changed;
    bool m_selected;
    std::function<void(const ConfigManager::Station&)> m_selectCallback;
};

class MenuScreen : public UIScreen {
public:
    MenuScreen(UIManager* uiManager);
    void draw(IDisplay& display) override;
    void interrupt() override;
    void handleInput(InputAction action) override;
    void setSelectCallback(std::function<void(int)> cb);

private:
    std::vector<std::string> m_options;
    UIManager* m_uiManager;
    size_t m_index;
    std::atomic<bool> m_interrupted;
    std::atomic<bool> m_changed;
    std::function<void(int)> m_selectCallback;
};

#endif // SCREENS_HPP
