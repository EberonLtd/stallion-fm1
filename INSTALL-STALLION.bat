@echo off
rem SPDX-License-Identifier: GPL-3.0-only
title Stallion installer
rem Usage: INSTALL-STALLION.bat        (8 voices)
rem        INSTALL-STALLION.bat 12     (12 voices: the CPU test, see DEMARRAGE-RAPIDE-FR.md)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build-stallion.ps1" %1
pause
