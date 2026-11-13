@echo off
rem SPDX-License-Identifier: GPL-3.0-only
title Stallion - publish on GitHub
rem Publishes Stallion 1.0 on your GitHub: the source, the release, the installer (GitHub Pages).
rem Run INSTALL-STALLION.bat first (it builds the firmware the release and the installer publish).
rem Usage: PUBLISH-GITHUB.bat                 (the repository YOUR-NAME/stallion-fm1)
rem        PUBLISH-GITHUB.bat -Repo another-name
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\publish_github.ps1" %*
pause
