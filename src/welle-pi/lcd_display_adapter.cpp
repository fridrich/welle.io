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
#include "lcd_display_adapter.hpp"

void LCDDisplayAdapter::clear() {
    m_display.clear();
}

void LCDDisplayAdapter::gotoXY(int x, int y) {
    m_display.gotoXY(x, y);
}

void LCDDisplayAdapter::write(const char* text) {
    m_display.write(text);
}

void LCDDisplayAdapter::killEOL() {
    m_display.killEOL();
}

void LCDDisplayAdapter::gotoLastLine() {
    m_display.gotoLastLine();
}

bool LCDDisplayAdapter::scroll(const char* text) {
    return m_display.scroll(text);
}

void LCDDisplayAdapter::interrupt() {
    m_display.interrupt();
}

void LCDDisplayAdapter::backlightOn() {
    m_display.backlightOn();
}

void LCDDisplayAdapter::backlightOff() {
    m_display.backlightOff();
}
