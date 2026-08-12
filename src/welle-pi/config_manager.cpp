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
#include "config_manager.hpp"
#include "libs/json.hpp"
#include <fstream>
#include <iostream>

ConfigManager::ConfigManager(const std::string& filename) : m_filename(filename) {}

bool ConfigManager::loadConfig() {
    std::ifstream f(m_filename);
    if (!f.is_open()) {
        return false;
    }
    try {
        nlohmann::json j;
        f >> j;
        m_stations.clear();
        auto lp_it = j.find("last_played");
        if (lp_it != j.end()) {
            auto lp = *lp_it;
            auto ch_it = lp.find("channel");
            if (ch_it != lp.end()) {
                m_lastPlayedChannel = ch_it->get<std::string>();
            }
            auto pr_it = lp.find("program");
            if (pr_it != lp.end()) {
                m_lastPlayedProgram = pr_it->get<std::string>();
            }
        }
        auto st_it = j.find("stations");
        if (st_it != j.end()) {
            for (const auto& item : *st_it) {
                Station s;
                auto ch_it = item.find("channel");
                if (ch_it != item.end()) s.channel = ch_it->get<std::string>();
                auto pr_it = item.find("program");
                if (pr_it != item.end()) s.program = pr_it->get<std::string>();
                auto id_it = item.find("service_id");
                if (id_it != item.end()) s.service_id = id_it->get<uint32_t>();
                m_stations.push_back(s);
            }
        }
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error parsing config file: " << e.what() << std::endl;
        return false;
    }
}

void ConfigManager::saveConfig() {
    try {
        nlohmann::json j;
        j["last_played"] = {
            {"channel", m_lastPlayedChannel},
            {"program", m_lastPlayedProgram}
        };
        nlohmann::json stations_json = nlohmann::json::array();
        for (const auto& s : m_stations) {
            stations_json.push_back({
                {"channel", s.channel},
                {"program", s.program},
                {"service_id", s.service_id}
            });
        }
        j["stations"] = stations_json;

        std::ofstream f(m_filename);
        if (f.is_open()) {
            f << j.dump(2);
        } else {
            std::cerr << "Failed to open " << m_filename << " for writing" << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error saving config file: " << e.what() << std::endl;
    }
}

const std::vector<ConfigManager::Station>& ConfigManager::getStations() const { return m_stations; }

void ConfigManager::addStation(const Station& s) {
    for (const auto& existing : m_stations) {
        if (existing.channel == s.channel && existing.program == s.program && existing.service_id == s.service_id) {
            return;
        }
    }
    m_stations.push_back(s);
}

void ConfigManager::clearStations() {
    m_stations.clear();
}

std::string ConfigManager::getLastPlayedChannel() const { return m_lastPlayedChannel; }
std::string ConfigManager::getLastPlayedProgram() const { return m_lastPlayedProgram; }

void ConfigManager::setLastPlayed(const std::string& channel, const std::string& program) {
    m_lastPlayedChannel = channel;
    m_lastPlayedProgram = program;
}
