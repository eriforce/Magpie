#include "pch.h"
#include "CursorManager.h"
#include "Logger.h"
#include <magnification.h>
#include "Win32Utils.h"
#include "ScalingOptions.h"
#include "ScalingWindow.h"
#include "Renderer.h"

#pragma comment(lib, "Magnification.lib")

namespace Magpie::Core {

// 将源窗口的光标位置映射到缩放后的光标位置
// 当光标位于源窗口之外，与源窗口的距离不会缩放
static POINT SrcToScaling(POINT pt) noexcept {
	const Renderer& renderer = ScalingWindow::Get().Renderer();
	const RECT& srcRect = renderer.SrcRect();
	const RECT& destRect = renderer.DestRect();
	const RECT& scalingRect = ScalingWindow::Get().WndRect();

	POINT result;

	if (pt.x >= srcRect.right) {
		result.x = scalingRect.right + pt.x - srcRect.right;
	} else if (pt.x < srcRect.left) {
		result.x = scalingRect.left + pt.x - srcRect.left;
	} else {
		double pos = double(pt.x - srcRect.left) / (srcRect.right - srcRect.left - 1);
		result.x = std::lround(pos * (destRect.right - destRect.left - 1)) + destRect.left;
	}

	if (pt.y >= srcRect.bottom) {
		result.y = scalingRect.bottom + pt.y - srcRect.bottom;
	} else if (pt.y < srcRect.top) {
		result.y = scalingRect.top + pt.y - srcRect.top;
	} else {
		double pos = double(pt.y - srcRect.top) / (srcRect.bottom - srcRect.top - 1);
		result.y = std::lround(pos * (destRect.bottom - destRect.top - 1)) + destRect.top;
	}

	return result;
}

static POINT ScalingToSrc(POINT pt) noexcept {
	const Renderer& renderer = ScalingWindow::Get().Renderer();
	const RECT& srcRect = renderer.SrcRect();
	const RECT& destRect = renderer.DestRect();

	const SIZE srcSize = Win32Utils::GetSizeOfRect(srcRect);
	const SIZE destSize = Win32Utils::GetSizeOfRect(destRect);

	POINT result = { srcRect.left, srcRect.top };

	if (pt.x >= destRect.right) {
		result.x += srcSize.cx + pt.x - destRect.right;
	} else if (pt.x < destRect.left) {
		result.x += pt.x - destRect.left;
	} else {
		double pos = double(pt.x - destRect.left) / (destSize.cx - 1);
		result.x += std::lround(pos * (srcSize.cx - 1));
	}

	if (pt.y >= destRect.bottom) {
		result.y += srcSize.cx + pt.y - destRect.bottom;
	} else if (pt.y < destRect.top) {
		result.y += pt.y - destRect.top;
	} else {
		double pos = double(pt.y - destRect.top) / (destSize.cy - 1);
		result.y += std::lround(pos * (srcSize.cy - 1));
	}

	return result;
}

CursorManager::~CursorManager() noexcept {
	if (_curClips != RECT{}) {
		ClipCursor(nullptr);
	}

	if (_isUnderCapture) {
		POINT pt{};
		if (!GetCursorPos(&pt)) {
			Logger::Get().Win32Error("GetCursorPos 失败");
		}
		_curClips = {};
		_StopCapture(pt, true);
	}
}

bool CursorManager::Initialize() noexcept {
	if (ScalingWindow::Get().Options().Is3DGameMode()) {
		POINT cursorPos;
		GetCursorPos(&cursorPos);
		_StartCapture(cursorPos);
	}

	Logger::Get().Info("CursorManager 初始化完成");
	return true;
}

void CursorManager::Update() noexcept {
	_UpdateCursorClip();

	_hCursor = NULL;

	const ScalingOptions& options = ScalingWindow::Get().Options();

	if (!options.IsDrawCursor() || !_isUnderCapture) {
		// 不绘制光标
		return;
	}

	CURSORINFO ci{ sizeof(CURSORINFO) };
	if (!GetCursorInfo(&ci)) {
		Logger::Get().Win32Error("GetCursorPos 失败");
		return;
	}

	if (ci.hCursor && ci.flags != CURSOR_SHOWING) {
		return;
	}

	_hCursor = ci.hCursor;
	_cursorPos = SrcToScaling(ci.ptScreenPos);
	const RECT& scalingRect = ScalingWindow::Get().WndRect();
	_cursorPos.x -= scalingRect.left;
	_cursorPos.y -= scalingRect.top;
}

void CursorManager::_ShowSystemCursor(bool show) {
	static void (WINAPI* const showSystemCursor)(BOOL bShow) = []()->void(WINAPI*)(BOOL) {
		HMODULE lib = LoadLibrary(L"user32.dll");
		if (!lib) {
			return nullptr;
		}

		return (void(WINAPI*)(BOOL))GetProcAddress(lib, "ShowSystemCursor");
	}();

	if (showSystemCursor) {
		showSystemCursor((BOOL)show);
	} else {
		// 获取 ShowSystemCursor 失败则回落到 Magnification API
		static bool initialized = []() {
			if (!MagInitialize()) {
				Logger::Get().Win32Error("MagInitialize 失败");
				return false;
			}

			return true;
		}();

		if (initialized) {
			MagShowSystemCursor(show);
		}
	}

	ScalingWindow::Get().Renderer().OnCursorVisibilityChanged(show);
}

void CursorManager::_AdjustCursorSpeed() noexcept {
	if (!SystemParametersInfo(SPI_GETMOUSESPEED, 0, &_originCursorSpeed, 0)) {
		Logger::Get().Win32Error("获取光标移速失败");
		return;
	}

	// 鼠标加速默认打开
	bool isMouseAccelerationOn = true;
	{
		std::array<INT, 3> values{};
		if (SystemParametersInfo(SPI_GETMOUSE, 0, values.data(), 0)) {
			isMouseAccelerationOn = values[2];
		} else {
			Logger::Get().Win32Error("检索鼠标加速失败");
		}
	}

	const Renderer& renderer = ScalingWindow::Get().Renderer();
	const SIZE srcSize = Win32Utils::GetSizeOfRect(renderer.SrcRect());
	const SIZE destSize = Win32Utils::GetSizeOfRect(renderer.DestRect());
	const double scale = ((double)destSize.cx / srcSize.cx + (double)destSize.cy / srcSize.cy) / 2;

	INT newSpeed = 0;

	// “提高指针精确度”（鼠标加速）打开时光标移速的调整为线性，否则为非线性
	// 参见 https://liquipedia.net/counterstrike/Mouse_Settings#Windows_Sensitivity
	if (isMouseAccelerationOn) {
		newSpeed = std::clamp((INT)lround(_originCursorSpeed / scale), 1, 20);
	} else {
		static constexpr std::array<double, 20> SENSITIVITIES = {
			0.03125, 0.0625, 0.125, 0.25, 0.375, 0.5, 0.625, 0.75, 0.875,
			1.0, 1.25, 1.5, 1.75, 2, 2.25, 2.5, 2.75, 3, 3.25, 3.5
		};

		_originCursorSpeed = std::clamp(_originCursorSpeed, 1, 20);
		double newSensitivity = SENSITIVITIES[static_cast<size_t>(_originCursorSpeed) - 1] / scale;

		auto it = std::lower_bound(SENSITIVITIES.begin(), SENSITIVITIES.end(), newSensitivity - 1e-6);
		newSpeed = INT(it - SENSITIVITIES.begin()) + 1;

		if (it != SENSITIVITIES.begin() && it != SENSITIVITIES.end()) {
			// 找到两侧最接近的数值
			if (std::abs(*it - newSensitivity) > std::abs(*(it - 1) - newSensitivity)) {
				--newSpeed;
			}
		}
	}

	if (!SystemParametersInfo(SPI_SETMOUSESPEED, 0, (PVOID)(intptr_t)newSpeed, 0)) {
		Logger::Get().Win32Error("设置光标移速失败");
	}
}

// 检测光标位于哪个窗口上，是否检测缩放窗口由 clickThroughHost 指定
static HWND WindowFromPoint(HWND hwndScaling, const RECT& scalingWndRect, POINT pt, bool clickThroughHost) noexcept {
	struct EnumData {
		HWND result;
		HWND hwndScaling;
		RECT scalingWndRect;
		POINT pt;
		bool clickThroughHost;
	} data{ NULL, hwndScaling, scalingWndRect, pt, clickThroughHost };

	EnumWindows([](HWND hWnd, LPARAM lParam) {
		EnumData& data = *(EnumData*)lParam;
		if (hWnd == data.hwndScaling) {
			if (PtInRect(&data.scalingWndRect, data.pt) && !data.clickThroughHost) {
				data.result = hWnd;
				return FALSE;
			} else {
				return TRUE;
			}
		}

		// 跳过不可见的窗口
		if (!(GetWindowLongPtr(hWnd, GWL_STYLE) & WS_VISIBLE)) {
			return TRUE;
		}

		// 跳过透明窗口
		if (GetWindowLongPtr(hWnd, GWL_EXSTYLE) & WS_EX_TRANSPARENT) {
			return TRUE;
		}

		// 跳过被冻结的窗口
		UINT isCloaked{};
		DwmGetWindowAttribute(hWnd, DWMWA_CLOAKED, &isCloaked, sizeof(isCloaked));
		if (isCloaked != 0) {
			return TRUE;
		}

		// 对于分层窗口（Layered Window），没有公开的 API 可以检测某个像素是否透明。
		// ChildWindowFromPointEx 是一个替代方案，当命中透明像素时它将返回 NULL。
		// Windows 内部有 LayerHitTest (https://github.com/tongzx/nt5src/blob/daad8a087a4e75422ec96b7911f1df4669989611/Source/XPSP1/NT/windows/core/ntuser/kernel/winwhere.c#L21) 方法用于对分层窗口执行命中测试，虽然它没有被公开，但 ChildWindowFromPointEx 使用了它
		// 在比 Magpie 权限更高的窗口上使用会失败，失败则假设不是分层窗口
		POINT clientPt = data.pt;
		ScreenToClient(hWnd, &clientPt);
		SetLastError(0);
		if (!ChildWindowFromPointEx(hWnd, clientPt, CWP_SKIPDISABLED | CWP_SKIPINVISIBLE | CWP_SKIPTRANSPARENT)) {
			if (GetLastError() == 0) {
				// 命中了透明像素
				return TRUE;
			}

			// 源窗口的权限比 Magpie 更高，回落到 GetWindowRect
			RECT windowRect{};
			if (!GetWindowRect(hWnd, &windowRect) || !PtInRect(&windowRect, data.pt)) {
				return TRUE;
			}
		}

		data.result = hWnd;
		return FALSE;
	}, (LPARAM)&data);

	return data.result;
}

void CursorManager::_UpdateCursorClip() noexcept {
	const Renderer& renderer = ScalingWindow::Get().Renderer();
	const RECT& srcRect = renderer.SrcRect();
	const RECT& destRect = renderer.DestRect();

	// 优先级：
	// 1. 断点模式：不限制，捕获/取消捕获，支持 UI
	// 2. 在 3D 游戏中限制光标：每帧都限制一次，不退出捕获，因此无法使用 UI，不支持多屏幕
	// 3. 常规：根据多屏幕限制光标，捕获/取消捕获，支持 UI 和多屏幕

	const ScalingOptions& options = ScalingWindow::Get().Options();
	if (!options.IsDebugMode() && options.Is3DGameMode()) {
		// 开启“在 3D 游戏中限制光标”则每帧都限制一次光标
		_curClips = srcRect;
		ClipCursor(&srcRect);
		return;
	}

	const HWND hwndScaling = ScalingWindow::Get().Handle();
	const RECT scalingRect = ScalingWindow::Get().WndRect();
	const HWND hwndSrc = ScalingWindow::Get().HwndSrc();
	
	INT_PTR style = GetWindowLongPtr(hwndScaling, GWL_EXSTYLE);

	POINT cursorPos;
	if (!GetCursorPos(&cursorPos)) {
		Logger::Get().Win32Error("GetCursorPos 失败");
		return;
	}

	if (_isUnderCapture) {
		///////////////////////////////////////////////////////////
		// 
		// 处于捕获状态
		// --------------------------------------------------------
		//                  |  虚拟位置被遮挡  |    虚拟位置未被遮挡
		// --------------------------------------------------------
		// 实际位置被遮挡    |    退出捕获     | 退出捕获，主窗口不透明
		// --------------------------------------------------------
		// 实际位置未被遮挡  |    退出捕获      |        无操作
		// --------------------------------------------------------
		// 
		///////////////////////////////////////////////////////////

		HWND hwndCur = WindowFromPoint(hwndScaling, scalingRect, SrcToScaling(cursorPos), false);

		if (hwndCur != hwndScaling) {
			// 主窗口被遮挡
			if (style | WS_EX_TRANSPARENT) {
				SetWindowLongPtr(hwndScaling, GWL_EXSTYLE, style & ~WS_EX_TRANSPARENT);
			}

			_StopCapture(cursorPos);
		} else {
			// 主窗口未被遮挡
			bool stopCapture = false;

			if (!stopCapture) {
				// 判断源窗口是否被遮挡
				hwndCur = WindowFromPoint(hwndScaling, scalingRect, cursorPos, true);
				stopCapture = hwndCur != hwndSrc && (!IsChild(hwndSrc, hwndCur) || !((GetWindowStyle(hwndCur) & WS_CHILD)));
			}

			if (stopCapture) {
				if (style | WS_EX_TRANSPARENT) {
					SetWindowLongPtr(hwndScaling, GWL_EXSTYLE, style & ~WS_EX_TRANSPARENT);
				}

				_StopCapture(cursorPos);
			} else {
				if (!(style & WS_EX_TRANSPARENT)) {
					SetWindowLongPtr(hwndScaling, GWL_EXSTYLE, style | WS_EX_TRANSPARENT);
				}
			}
		}
	} else {
		/////////////////////////////////////////////////////////
		// 
		// 未处于捕获状态
		// -----------------------------------------------------
		//					|  虚拟位置被遮挡	|  虚拟位置未被遮挡
		// ------------------------------------------------------
		// 实际位置被遮挡		|    无操作		|    主窗口不透明
		// ------------------------------------------------------
		// 实际位置未被遮挡	|    无操作		| 开始捕获，主窗口透明
		// ------------------------------------------------------
		// 
		/////////////////////////////////////////////////////////

		HWND hwndCur = WindowFromPoint(hwndScaling, scalingRect, cursorPos, false);

		if (hwndCur == hwndScaling) {
			// 主窗口未被遮挡
			POINT newCursorPos = ScalingToSrc(cursorPos);

			if (!PtInRect(&srcRect, newCursorPos)) {
				// 跳过黑边
				if (false) {
					// 从内部移到外部
					// 此时有 UI 贴边
					/*if (newCursorPos.x >= _srcRect.right) {
						cursorPos.x += _scalingWndRect.right - _scalingWndRect.left - outputRect.right;
					} else if (newCursorPos.x < _srcRect.left) {
						cursorPos.x -= outputRect.left;
					}

					if (newCursorPos.y >= _srcRect.bottom) {
						cursorPos.y += _scalingWndRect.bottom - _scalingWndRect.top - outputRect.bottom;
					} else if (newCursorPos.y < _srcRect.top) {
						cursorPos.y -= outputRect.top;
					}

					if (MonitorFromPoint(cursorPos, MONITOR_DEFAULTTONULL)) {
						SetCursorPos(cursorPos.x, cursorPos.y);
					} else {
						// 目标位置不存在屏幕，则将光标限制在输出区域内
						SetCursorPos(
							std::clamp(cursorPos.x, _scalingWndRect.left + outputRect.left, _scalingWndRect.left + outputRect.right - 1),
							std::clamp(cursorPos.y, _scalingWndRect.top + outputRect.top, _scalingWndRect.top + outputRect.bottom - 1)
						);
					}*/
				} else {
					// 从外部移到内部

					POINT clampedPos = {
						std::clamp(cursorPos.x, destRect.left, destRect.right - 1),
						std::clamp(cursorPos.y, destRect.top, destRect.bottom - 1)
					};

					if (WindowFromPoint(hwndScaling, scalingRect, clampedPos, false) == hwndScaling) {
						if (!(style & WS_EX_TRANSPARENT)) {
							SetWindowLongPtr(hwndScaling, GWL_EXSTYLE, style | WS_EX_TRANSPARENT);
						}

						_StartCapture(cursorPos);
					} else {
						// 要跳跃的位置被遮挡
						if (style | WS_EX_TRANSPARENT) {
							SetWindowLongPtr(hwndScaling, GWL_EXSTYLE, style & ~WS_EX_TRANSPARENT);
						}
					}
				}
			} else {
				bool startCapture = true;

				if (startCapture) {
					// 判断源窗口是否被遮挡
					hwndCur = WindowFromPoint(hwndScaling, scalingRect, newCursorPos, true);
					startCapture = hwndCur == hwndSrc || ((IsChild(hwndSrc, hwndCur) && (GetWindowStyle(hwndCur) & WS_CHILD)));
				}

				if (startCapture) {
					if (!(style & WS_EX_TRANSPARENT)) {
						SetWindowLongPtr(hwndScaling, GWL_EXSTYLE, style | WS_EX_TRANSPARENT);
					}

					_StartCapture(cursorPos);
				} else {
					if (style | WS_EX_TRANSPARENT) {
						SetWindowLongPtr(hwndScaling, GWL_EXSTYLE, style & ~WS_EX_TRANSPARENT);
					}
				}
			}
		}
	}

	if (options.IsDebugMode()) {
		return;
	}

	if (!false && !_isUnderCapture) {
		return;
	}

	// 根据当前光标位置的四个方向有无屏幕来确定应该在哪些方向限制光标，但这无法
	// 处理屏幕之间存在间隙的情况。解决办法是 _StopCapture 只在目标位置存在屏幕时才取消捕获，
	// 当光标试图移动到间隙中时将被挡住。如果光标的速度足以跨越间隙，则它依然可以在屏幕间移动。
	::GetCursorPos(&cursorPos);
	POINT hostPos = false ? cursorPos : SrcToScaling(cursorPos);

	RECT clips{ LONG_MIN, LONG_MIN, LONG_MAX, LONG_MAX };

	// left
	RECT rect{ LONG_MIN, hostPos.y, scalingRect.left, hostPos.y + 1 };
	if (!MonitorFromRect(&rect, MONITOR_DEFAULTTONULL)) {
		clips.left = false ? destRect.left : srcRect.left;
	}

	// top
	rect = { hostPos.x, LONG_MIN, hostPos.x + 1,scalingRect.top };
	if (!MonitorFromRect(&rect, MONITOR_DEFAULTTONULL)) {
		clips.top = false ? destRect.top : srcRect.top;
	}

	// right
	rect = { scalingRect.right, hostPos.y, LONG_MAX, hostPos.y + 1 };
	if (!MonitorFromRect(&rect, MONITOR_DEFAULTTONULL)) {
		clips.right = false ? destRect.right : srcRect.right;
	}

	// bottom
	rect = { hostPos.x, scalingRect.bottom, hostPos.x + 1, LONG_MAX };
	if (!MonitorFromRect(&rect, MONITOR_DEFAULTTONULL)) {
		clips.bottom = false ? destRect.bottom : srcRect.bottom;
	}

	if (clips != _curClips) {
		_curClips = clips;
		ClipCursor(&clips);
	}
}

void CursorManager::_StartCapture(POINT cursorPos) noexcept {
	if (_isUnderCapture) {
		return;
	}

	const Renderer& renderer = ScalingWindow::Get().Renderer();
	const RECT& srcRect = renderer.SrcRect();
	const RECT& destRect = renderer.DestRect();

	// 在以下情况下进入捕获状态：
	// 1. 当前未捕获
	// 2. 光标进入全屏区域
	// 
	// 进入捕获状态时：
	// 1. 调整光标速度，全局隐藏光标
	// 2. 将光标移到源窗口的对应位置
	//
	// 在有黑边的情况下自动将光标调整到画面内

	// 全局隐藏光标
	_ShowSystemCursor(false);

	SIZE srcFrameSize = Win32Utils::GetSizeOfRect(srcRect);
	SIZE outputSize = Win32Utils::GetSizeOfRect(destRect);

	if (ScalingWindow::Get().Options().IsAdjustCursorSpeed()) {
		_AdjustCursorSpeed();
	}

	// 移动光标位置

	// 跳过黑边
	cursorPos.x = std::clamp(cursorPos.x, destRect.left, destRect.right - 1);
	cursorPos.y = std::clamp(cursorPos.y, destRect.top, destRect.bottom - 1);

	POINT newCursorPos = ScalingToSrc(cursorPos);
	SetCursorPos(newCursorPos.x, newCursorPos.y);

	_isUnderCapture = true;
}

void CursorManager::_StopCapture(POINT cursorPos, bool onDestroy) noexcept {
	if (!_isUnderCapture) {
		return;
	}

	if (_curClips != RECT{}) {
		_curClips = {};
		ClipCursor(nullptr);
	}

	// 在以下情况下离开捕获状态：
	// 1. 当前处于捕获状态
	// 2. 光标离开源窗口客户区
	// 3. 目标位置存在屏幕
	//
	// 离开捕获状态时
	// 1. 还原光标速度，全局显示光标
	// 2. 将光标移到全屏窗口外的对应位置
	//
	// 在有黑边的情况下自动将光标调整到全屏窗口外

	POINT newCursorPos = SrcToScaling(cursorPos);

	if (onDestroy || MonitorFromPoint(newCursorPos, MONITOR_DEFAULTTONULL)) {
		SetCursorPos(newCursorPos.x, newCursorPos.y);

		if (ScalingWindow::Get().Options().IsAdjustCursorSpeed()) {
			SystemParametersInfo(SPI_SETMOUSESPEED, 0, (PVOID)(intptr_t)_originCursorSpeed, 0);
		}

		_ShowSystemCursor(true);
		_isUnderCapture = false;
	} else {
		// 目标位置不存在屏幕，则将光标限制在源窗口内
		const RECT& srcRect = ScalingWindow::Get().Renderer().SrcRect();
		SetCursorPos(
			std::clamp(cursorPos.x, srcRect.left, srcRect.right - 1),
			std::clamp(cursorPos.y, srcRect.top, srcRect.bottom - 1)
		);
	}
}

}
