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
#ifndef DISPLAY_INTERFACE_HPP
#define DISPLAY_INTERFACE_HPP

class IDisplay {
public:
    virtual ~IDisplay() = default;
    virtual void clear() = 0;
    virtual void gotoXY(int x, int y) = 0;
    virtual void write(const char* text) = 0;
    virtual void killEOL() = 0;
    virtual void gotoLastLine() = 0;
    virtual bool scroll(const char* text) = 0;
    virtual void interrupt() = 0;
    virtual void backlightOn() {}
    virtual void backlightOff() {}
    virtual int getWidth() const { return 16; }
    virtual int getHeight() const { return 2; }
};

#endif // DISPLAY_INTERFACE_HPP
