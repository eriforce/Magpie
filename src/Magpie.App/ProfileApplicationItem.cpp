#include "pch.h"
#include "ProfileApplicationItem.h"
#if __has_include("ProfileApplicationItem.g.cpp")
#include "ProfileApplicationItem.g.cpp"
#endif
#include "Profile.h"
#include "ProfileApplication.h"
#include "AppXReader.h"
#include "ProfileService.h"
#include "IconHelper.h"
#include "Win32Utils.h"
#include "Logger.h"

using namespace winrt;
using namespace Windows::ApplicationModel::Resources;
using namespace Windows::Graphics::Display;
using namespace Windows::Graphics::Imaging;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::UI::Xaml::Media::Imaging;
using namespace ::Magpie::Core;

namespace winrt::Magpie::App::implementation {

static void LaunchPackagedApp(const ProfileApplication& profile) {
	// 关于启动打包应用的讨论：
	// https://github.com/microsoft/WindowsAppSDK/issues/2856#issuecomment-1224409948
	// 使用 CLSCTX_LOCAL_SERVER 以在独立的进程中启动应用
	// 见 https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nn-shobjidl_core-iapplicationactivationmanager
	com_ptr<IApplicationActivationManager> aam =
		try_create_instance<IApplicationActivationManager>(CLSID_ApplicationActivationManager, CLSCTX_LOCAL_SERVER);
	if (!aam) {
		Logger::Get().Error("创建 ApplicationActivationManager 失败");
		return;
	}

	// 确保启动为前台窗口
	HRESULT hr = CoAllowSetForegroundWindow(aam.get(), nullptr);
	if (FAILED(hr)) {
		Logger::Get().ComError("创建 CoAllowSetForegroundWindow 失败", hr);
	}

	DWORD procId;
	hr = aam->ActivateApplication(profile.pathRule.c_str(), profile.launchParameters.c_str(), AO_NONE, &procId);
	if (FAILED(hr)) {
		Logger::Get().ComError("IApplicationActivationManager::ActivateApplication 失败", hr);
		return;
	}
}

ProfileApplicationItem::ProfileApplicationItem(uint32_t profileIdx, uint32_t applicationIdx)
	: _profileIdx(profileIdx), _applicationIdx(applicationIdx) {
	_application = &ProfileService::Get().GetProfile(_profileIdx).applications[_applicationIdx];

	_path = _application->pathRule;

	if (_application->isPackaged) {
		AppXReader appxReader;
		_exists = appxReader.Initialize(_application->pathRule);
	} else {
		_exists = Win32Utils::FileExists(_application->pathRule.c_str());
	}

	_LoadIcon();

	_applicationRemovedRevoker = ProfileService::Get().ApplicationRemoved(
		auto_revoke, { this, &ProfileApplicationItem::_ProfileService_ApplicationRemoved });
}

void ProfileApplicationItem::_ProfileService_ApplicationRemoved(uint32_t, uint32_t applicationIdx) {
	if (_applicationIdx > applicationIdx) {
		_applicationIdx--;
	}
}

void ProfileApplicationItem::Launch() const noexcept {
	if (_application->isPackaged) {
		LaunchPackagedApp(*_application);
	} else {
		Win32Utils::ShellOpen(_application->pathRule.c_str(), _application->launchParameters.c_str());
	}
}

fire_and_forget ProfileApplicationItem::OpenProgramLocation() const noexcept {
	std::wstring programLocation;
	if (_application->isPackaged) {
		AppXReader appxReader;
		[[maybe_unused]] bool result = appxReader.Initialize(_application->pathRule);
		assert(result);

		programLocation = appxReader.GetExecutablePath();
		if (programLocation.empty()) {
			// 找不到可执行文件则打开应用文件夹
			Win32Utils::ShellOpen(appxReader.GetPackagePath().c_str());
			co_return;
		}
	} else {
		programLocation = _application->pathRule;
	}

	co_await resume_background();
	Win32Utils::OpenFolderAndSelectFile(programLocation.c_str());
}

void ProfileApplicationItem::Remove() {
	ProfileService::Get().RemoveApplication(_profileIdx, _applicationIdx);
	_applicationIdx = std::numeric_limits<uint32_t>::max();
}

fire_and_forget ProfileApplicationItem::_LoadIcon() {
	static constexpr const UINT ICON_SIZE = 32;
	std::wstring iconPath;
	SoftwareBitmap iconBitmap{ nullptr };

	if (_exists) {
		auto weakThis = get_weak();

		App app = Application::Current().as<App>();
		MainPage mainPage = app.MainPage();
		const bool preferLightTheme = mainPage.ActualTheme() == ElementTheme::Light;
		DisplayInformation _displayInformation = DisplayInformation::GetForCurrentView();
		const uint32_t dpi = (uint32_t)std::lroundf(_displayInformation.LogicalDpi());
		CoreDispatcher dispatcher = mainPage.Dispatcher();

		co_await resume_background();

		if (_application->isPackaged) {
			AppXReader appxReader;
			[[maybe_unused]] bool result = appxReader.Initialize(_application->pathRule);
			assert(result);

			std::variant<std::wstring, SoftwareBitmap> uwpIcon =
				appxReader.GetIcon((uint32_t)std::ceil(dpi * ICON_SIZE / double(USER_DEFAULT_SCREEN_DPI)), preferLightTheme);
			if (uwpIcon.index() == 0) {
				iconPath = std::get<0>(uwpIcon);
			} else {
				iconBitmap = std::get<1>(uwpIcon);
			}
		} else {
			iconBitmap = IconHelper::ExtractIconFromExe(_application->pathRule.c_str(), ICON_SIZE, dpi);
		}

		co_await dispatcher;
		if (!weakThis.get()) {
			co_return;
		}
	}

	if (!iconPath.empty()) {
		BitmapIcon icon;
		icon.ShowAsMonochrome(false);
		icon.UriSource(Uri(iconPath));

		_icon = std::move(icon);
	} else if (iconBitmap) {
		SoftwareBitmapSource imageSource;
		co_await imageSource.SetBitmapAsync(iconBitmap);

		MUXC::ImageIcon imageIcon;
		imageIcon.Source(imageSource);

		_icon = std::move(imageIcon);
	} else {
		FontIcon icon;
		icon.Glyph(L"\uECE4");
		_icon = icon;
		_icon.Width(ICON_SIZE);
		_icon.Height(ICON_SIZE);
	}

	_propertyChangedEvent(*this, PropertyChangedEventArgs(L"Icon"));
}

}
