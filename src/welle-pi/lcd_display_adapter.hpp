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
#ifndef LCD_DISPLAY_ADAPTER_HPP
#define LCD_DISPLAY_ADAPTER_HPP

#include "display_interface.hpp"
#include <liblcd/liblcd.h>

class LCDDisplayAdapter : public IDisplay {
public:
    LCDDisplayAdapter() = default;
    ~LCDDisplayAdapter() override = default;

    void clear() override;
    void gotoXY(int x, int y) override;
    void write(const char* text) override;
    void killEOL() override;
    void gotoLastLine() override;
    bool scroll(const char* text) override;
    void interrupt() override;
    void backlightOn() override;
    void backlightOff() override;

private:
    liblcd::LCDDisplay m_display;
};

#endif // LCD_DISPLAY_ADAPTER_HPP
