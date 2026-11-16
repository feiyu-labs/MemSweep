// MemSweep — UI 接口
#pragma once
#include "common.h"

namespace ms {

int  RunUI(HINSTANCE hInst, bool showOnStart);
int  RenderUISnapshot(const std::wstring& path);
int  RenderSysCheckSnapshot(const std::wstring& path);

// 单实例转发
void UI_ForwardShow();
void UI_ForwardClean();
void UI_ForwardExit();
void UI_ForwardPause(int minutes);
void UI_ForwardCombine();

// 开机自启（计划任务）
bool AutostartInstalled();
bool AutostartSet(bool on);

} // namespace ms
