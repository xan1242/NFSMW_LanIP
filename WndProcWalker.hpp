/*
// clang-format off
//
//    LanIP
//    Copyright (c) 2023 Berkay Yigit <mail@berkay.link>
//    Copyright (c) 2023 Lovro Plese (Xan/Tenjoin)
//
//    This program is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Affero General Public License as published
//    by the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    This program is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
//    GNU Affero General Public License for more details.
//
//    You should have received a copy of the GNU Affero General Public License
//    along with this program. If not, see <https://www.gnu.org/licenses/>.
//
// clang-format on
*/

#ifndef MOD_WNDPROCWALKER_H
#define MOD_WNDPROCWALKER_H
#pragma once

#define GAME_HWND_ADDR 0x00982BF4

#include <Windows.h>
#include <vector>
#include <Winuser.h>  // MessageBox

namespace WndProcWalker {
    namespace details {
        inline constinit bool                           sIsWindowActive = true;
        inline constinit HWND                           sWindowHandle = nullptr;
        inline std::pair<WNDPROC, std::vector<WNDPROC>> sWndProc;

        inline LRESULT CALLBACK hkWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
            if (uMsg == WM_ACTIVATE || uMsg == WM_ACTIVATEAPP) sIsWindowActive = (LOWORD(wParam) != WA_INACTIVE) || wParam == TRUE;

            LRESULT ret = FALSE;
            for (const auto& wndproc : sWndProc.second) ret += wndproc(hWnd, uMsg, wParam, lParam);

            return ret ? FALSE : ::CallWindowProc(sWndProc.first, hWnd, uMsg, wParam, lParam);
        }
    }  // namespace details

    inline HWND GetWindowHandle() { return details::sWindowHandle; }
    inline bool IsWindowActive() { return details::sIsWindowActive; }
    inline void AddWndProc(WNDPROC fn) { details::sWndProc.second.push_back(fn); }

    inline void Init() {
        details::sWindowHandle = *reinterpret_cast<HWND*>(GAME_HWND_ADDR);
        details::sWndProc.first =
            reinterpret_cast<WNDPROC>(::SetWindowLongPtr(details::sWindowHandle, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&details::hkWndProc)));
    }
}  // namespace WndProcWalker

#endif