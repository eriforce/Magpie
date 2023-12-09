#include "pch.h"
#include "ProfileViewModel.h"
#if __has_include("ProfileViewModel.g.cpp")
#include "ProfileViewModel.g.cpp"
#endif
#include "Profile.h"
#include "AppXReader.h"
#include "IconHelper.h"
#include "ProfileService.h"
#include "StrUtils.h"
#include <Magpie.Core.h>
#include "Win32Utils.h"
#include "AppSettings.h"
#include "Logger.h"
#include "ScalingMode.h"
#include <dxgi.h>
#include "MagService.h"
#include "FileDialogHelper.h"

using namespace winrt;
using namespace Windows::Graphics::Display;
using namespace Windows::Graphics::Imaging;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::UI::Xaml::Media::Imaging;
using namespace ::Magpie::Core;

namespace winrt::Magpie::App::implementation {

static SmallVector<std::wstring> GetAllGraphicsCards() {
	com_ptr<IDXGIFactory1> dxgiFactory;
	HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&dxgiFactory));
	if (FAILED(hr)) {
		return {};
	}

	SmallVector<std::wstring> result;

	com_ptr<IDXGIAdapter1> adapter;
	for (UINT adapterIndex = 0;
		SUCCEEDED(dxgiFactory->EnumAdapters1(adapterIndex, adapter.put()));
		++adapterIndex
		) {
		DXGI_ADAPTER_DESC1 desc;
		hr = adapter->GetDesc1(&desc);

		// 不包含 WARP
		if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
			continue;
		}

		result.emplace_back(SUCCEEDED(hr) ? desc.Description : L"???");
	}

	return result;
}

ProfileViewModel::ProfileViewModel(int profileIdx) : _isDefaultProfile(profileIdx < 0) {
	if (_isDefaultProfile) {
		_data = &ProfileService::Get().DefaultProfile();
	} else {
		_index = (uint32_t)profileIdx;
		_data = &ProfileService::Get().GetProfile(profileIdx);
	}

	ResourceLoader resourceLoader = ResourceLoader::GetForCurrentView();
	{
		std::vector<IInspectable> scalingModes;
		scalingModes.push_back(box_value(resourceLoader.GetString(L"Profile_General_ScalingMode_None")));
		for (const Magpie::App::ScalingMode& sm : AppSettings::Get().ScalingModes()) {
			scalingModes.push_back(box_value(sm.name));
		}
		_scalingModes = single_threaded_vector(std::move(scalingModes));
	}

	{
		std::vector<IInspectable> captureMethods;
		captureMethods.reserve(4);
		captureMethods.push_back(box_value(L"Graphics Capture"));
		if (Win32Utils::GetOSVersion().Is20H1OrNewer()) {
			// Desktop Duplication 要求 Win10 20H1+
			captureMethods.push_back(box_value(L"Desktop Duplication"));
		}
		captureMethods.push_back(box_value(L"GDI"));
		captureMethods.push_back(box_value(L"DwmSharedSurface"));

		_captureMethods = single_threaded_vector(std::move(captureMethods));
	}

	{
		std::vector<IInspectable> applications;
		applications.reserve(_data->applications.size());
		for (UINT i = 0; i < _data->applications.size(); i++) {
			_applications.Append(ProfileApplicationItem(profileIdx, i));
		}
		_applications.VectorChanged({ this, &ProfileViewModel::_Applications_VectorChanged });
	}

	_graphicsCards = GetAllGraphicsCards();
	if (_data->graphicsCard >= _graphicsCards.size()) {
		_data->graphicsCard = -1;
	}

	_applicationAddedRevoker = ProfileService::Get().ApplicationAdded(
		auto_revoke, { this, &ProfileViewModel::_ProfileService_ApplicationAdded });
	_applicationRemovedRevoker = ProfileService::Get().ApplicationRemoved(
		auto_revoke, { this, &ProfileViewModel::_ProfileService_ApplicationRemoved });
	_is3DGameModeChangedRevoker = MagService::Get().Is3DGameModeChanged(
		auto_revoke, { this, &ProfileViewModel::_MagService_Is3DGameModeChanged });
}

ProfileViewModel::~ProfileViewModel() {}

void ProfileViewModel::_ProfileService_ApplicationAdded(uint32_t profileIdx, uint32_t applicationIdx) {
	_isMovingApplication = false;
	_applications.Append(ProfileApplicationItem(profileIdx, applicationIdx));
	_isMovingApplication = true;
}

void ProfileViewModel::_ProfileService_ApplicationRemoved(uint32_t, uint32_t applicationIdx) {
	_isMovingApplication = false;
	_applications.RemoveAt(applicationIdx);
	_isMovingApplication = true;
}

void ProfileViewModel::_Applications_VectorChanged(IObservableVector<IInspectable> const&, IVectorChangedEventArgs const& args) {
	if (!_isMovingApplication) {
		return;
	}

	if (args.CollectionChange() == CollectionChange::ItemRemoved) {
		_applicationMovingFromIdx = args.Index();
		return;
	}

	assert(args.CollectionChange() == CollectionChange::ItemInserted);
	uint32_t movingToIdx = args.Index();
	ProfileService::Get().MoveApplication(_index, _applicationMovingFromIdx, movingToIdx);

	uint32_t minIdx = std::min(_applicationMovingFromIdx, movingToIdx);
	uint32_t maxIdx = std::max(_applicationMovingFromIdx, movingToIdx);
	for (uint32_t i = minIdx; i <= maxIdx; ++i) {
		_applications.GetAt(i).as<ProfileApplicationItem>().ApplicationIdx(i);
	}
}

void ProfileViewModel::_MagService_Is3DGameModeChanged(bool) {
	_propertyChangedEvent(*this, PropertyChangedEventArgs(L"Is3DGameMode"));
}

bool ProfileViewModel::IsNotDefaultProfile() const noexcept {
	return !_data->name.empty();
}

hstring ProfileViewModel::Name() const noexcept {
	if (_data->name.empty()) {
		return ResourceLoader::GetForCurrentView().GetString(L"Root_Defaults/Content");
	} else {
		return hstring(_data->name);
	}
}

void ProfileViewModel::RenameText(const hstring& value) {
	_renameText = value;
	_propertyChangedEvent(*this, PropertyChangedEventArgs(L"RenameText"));

	_trimedRenameText = value;
	StrUtils::Trim(_trimedRenameText);
	bool newEnabled = !_trimedRenameText.empty() && _trimedRenameText != _data->name;
	if (_isRenameConfirmButtonEnabled != newEnabled) {
		_isRenameConfirmButtonEnabled = newEnabled;
		_propertyChangedEvent(*this, PropertyChangedEventArgs(L"IsRenameConfirmButtonEnabled"));
	}
}

void ProfileViewModel::Rename() {
	if (_isDefaultProfile || !_isRenameConfirmButtonEnabled) {
		return;
	}

	ProfileService::Get().RenameProfile(_index, _trimedRenameText);
	_propertyChangedEvent(*this, PropertyChangedEventArgs(L"Name"));
}

bool ProfileViewModel::CanMoveUp() const noexcept {
	return !_isDefaultProfile && _index != 0;
}

bool ProfileViewModel::CanMoveDown() const noexcept {
	return !_isDefaultProfile && _index + 1 < ProfileService::Get().GetProfileCount();
}

void ProfileViewModel::MoveUp() {
	if (_isDefaultProfile) {
		return;
	}

	ProfileService& profileService = ProfileService::Get();
	if (!profileService.MoveProfile(_index, true)) {
		return;
	}

	--_index;
	_data = &profileService.GetProfile(_index);

	_propertyChangedEvent(*this, PropertyChangedEventArgs(L"CanMoveUp"));
	_propertyChangedEvent(*this, PropertyChangedEventArgs(L"CanMoveDown"));
}

void ProfileViewModel::MoveDown() {
	if (_isDefaultProfile) {
		return;
	}

	ProfileService& profileService = ProfileService::Get();
	if (!profileService.MoveProfile(_index, false)) {
		return;
	}

	++_index;
	_data = &profileService.GetProfile(_index);

	_propertyChangedEvent(*this, PropertyChangedEventArgs(L"CanMoveUp"));
	_propertyChangedEvent(*this, PropertyChangedEventArgs(L"CanMoveDown"));
}

void ProfileViewModel::Delete() {
	if (_isDefaultProfile) {
		return;
	}

	ProfileService::Get().RemoveProfile(_index);
	_data = nullptr;
}

int ProfileViewModel::ScalingMode() const noexcept {
	return _data->scalingMode + 1;
}

void ProfileViewModel::ScalingMode(int value) {
	_data->scalingMode = value - 1;
	_propertyChangedEvent(*this, PropertyChangedEventArgs(L"ScalingMode"));

	AppSettings::Get().SaveAsync();
}

int ProfileViewModel::CaptureMethod() const noexcept {
	if (Win32Utils::GetOSVersion().Is20H1OrNewer() || _data->captureMethod < CaptureMethod::DesktopDuplication) {
		return (int)_data->captureMethod;
	} else {
		return (int)_data->captureMethod - 1;
	}
}

void ProfileViewModel::CaptureMethod(int value) {
	if (value < 0) {
		return;
	}

	if (!Win32Utils::GetOSVersion().Is20H1OrNewer() && value >= (int)CaptureMethod::DesktopDuplication) {
		++value;
	}

	::Magpie::Core::CaptureMethod captureMethod = (::Magpie::Core::CaptureMethod)value;
	if (_data->captureMethod == captureMethod) {
		return;
	}

	_data->captureMethod = captureMethod;
	_propertyChangedEvent(*this, PropertyChangedEventArgs(L"CaptureMethod"));
	_propertyChangedEvent(*this, PropertyChangedEventArgs(L"CanCaptureTitleBar"));

	AppSettings::Get().SaveAsync();
}

bool ProfileViewModel::IsAutoScale() const noexcept {
	return _data->isAutoScale;
}

void ProfileViewModel::IsAutoScale(bool value) {
	if (_data->isAutoScale == value) {
		return;
	}

	_data->isAutoScale = value;
	_propertyChangedEvent(*this, PropertyChangedEventArgs(L"IsAutoScale"));

	AppSettings::Get().SaveAsync();

	if (value) {
		// 立即检查前台窗口是否应自动缩放
		MagService::Get().CheckForeground();
	}
}

bool ProfileViewModel::Is3DGameMode() const noexcept {
	return _data->Is3DGameMode();
}

void ProfileViewModel::Is3DGameMode(bool value) {
	if (_data->Is3DGameMode() == value) {
		return;
	}

	_data->Is3DGameMode(value);
	_propertyChangedEvent(*this, PropertyChangedEventArgs(L"Is3DGameMode"));

	AppSettings::Get().SaveAsync();
}

bool ProfileViewModel::HasMultipleMonitors() const noexcept {
	return GetSystemMetrics(SM_CMONITORS) > 1;
}

int ProfileViewModel::MultiMonitorUsage() const noexcept {
	return (int)_data->multiMonitorUsage;
}

void ProfileViewModel::MultiMonitorUsage(int value) {
	if (value < 0) {
		return;
	}

	::Magpie::Core::MultiMonitorUsage multiMonitorUsage = (::Magpie::Core::MultiMonitorUsage)value;
	if (_data->multiMonitorUsage == multiMonitorUsage) {
		return;
	}

	_data->multiMonitorUsage = multiMonitorUsage;
	_propertyChangedEvent(*this, PropertyChangedEventArgs(L"MultiMonitorUsage"));

	AppSettings::Get().SaveAsync();
}

IVector<IInspectable> ProfileViewModel::GraphicsCards() const noexcept {
	std::vector<IInspectable> graphicsCards;
	graphicsCards.reserve(_graphicsCards.size() + 1);

	ResourceLoader resourceLoader = ResourceLoader::GetForCurrentView();
	hstring defaultStr = resourceLoader.GetString(L"Profile_General_CaptureMethod_Default");
	graphicsCards.push_back(box_value(defaultStr));

	for (const std::wstring& graphicsCard : _graphicsCards) {
		graphicsCards.push_back(box_value(graphicsCard));
	}

	return single_threaded_vector(std::move(graphicsCards));
}

int ProfileViewModel::GraphicsCard() const noexcept {
	return _data->graphicsCard + 1;
}

void ProfileViewModel::GraphicsCard(int value) {
	if (value < 0) {
		return;
	}

	--value;
	if (_data->graphicsCard == value) {
		return;
	}

	_data->graphicsCard = value;
	_propertyChangedEvent(*this, PropertyChangedEventArgs(L"GraphicsCard"));

	AppSettings::Get().SaveAsync();
}

bool ProfileViewModel::IsShowGraphicsCardSettingsCard() const noexcept {
	return _graphicsCards.size() > 1;
}

bool ProfileViewModel::IsShowFPS() const noexcept {
	return _data->IsShowFPS();
}

void ProfileViewModel::IsShowFPS(bool value) {
	if (_data->IsShowFPS() == value) {
		return;
	}

	_data->IsShowFPS(value);
	_propertyChangedEvent(*this, PropertyChangedEventArgs(L"IsShowFPS"));

	AppSettings::Get().SaveAsync();
}

bool ProfileViewModel::IsVSync() const noexcept {
	return _data->IsVSync();
}

void ProfileViewModel::IsVSync(bool value) {
	if (_data->IsVSync() == value) {
		return;
	}

	_data->IsVSync(value);
	_propertyChangedEvent(*this, PropertyChangedEventArgs(L"IsVSync"));

	AppSettings::Get().SaveAsync();
}

bool ProfileViewModel::IsTripleBuffering() const noexcept {
	return _data->IsTripleBuffering();
}

void ProfileViewModel::IsTripleBuffering(bool value) {
	if (_data->IsTripleBuffering() == value) {
		return;
	}

	_data->IsTripleBuffering(value);
	_propertyChangedEvent(*this, PropertyChangedEventArgs(L"IsTripleBuffering"));

	AppSettings::Get().SaveAsync();
}

bool ProfileViewModel::IsDisableWindowResizing() const noexcept {
	return _data->IsDisableWindowResizing();
}

void ProfileViewModel::IsDisableWindowResizing(bool value) {
	if (_data->IsDisableWindowResizing() == value) {
		return;
	}

	_data->IsDisableWindowResizing(value);
	_propertyChangedEvent(*this, PropertyChangedEventArgs(L"IsDisableWindowResizing"));

	AppSettings::Get().SaveAsync();
}

bool ProfileViewModel::IsCaptureTitleBar() const noexcept {
	return _data->IsCaptureTitleBar();
}

void ProfileViewModel::IsCaptureTitleBar(bool value) {
	if (_data->IsCaptureTitleBar() == value) {
		return;
	}

	_data->IsCaptureTitleBar(value);
	_propertyChangedEvent(*this, PropertyChangedEventArgs(L"IsCaptureTitleBar"));

	AppSettings::Get().SaveAsync();
}

bool ProfileViewModel::CanCaptureTitleBar() const noexcept {
	return _data->captureMethod == CaptureMethod::GraphicsCapture
		|| _data->captureMethod == CaptureMethod::DesktopDuplication;
}

bool ProfileViewModel::IsCroppingEnabled() const noexcept {
	return _data->isCroppingEnabled;
}

void ProfileViewModel::IsCroppingEnabled(bool value) {
	if (_data->isCroppingEnabled == value) {
		return;
	}

	_data->isCroppingEnabled = value;
	_propertyChangedEvent(*this, PropertyChangedEventArgs(L"IsCroppingEnabled"));

	AppSettings::Get().SaveAsync();
}

double ProfileViewModel::CroppingLeft() const noexcept {
	return _data->cropping.Left;
}

void ProfileViewModel::CroppingLeft(double value) {
	if (_data->cropping.Left == value) {
		return;
	}

	_data->cropping.Left = std::isnan(value) ? 0.0f : (float)value;
	_propertyChangedEvent(*this, PropertyChangedEventArgs(L"CroppingLeft"));

	AppSettings::Get().SaveAsync();
}

double ProfileViewModel::CroppingTop() const noexcept {
	return _data->cropping.Top;
}

void ProfileViewModel::CroppingTop(double value) {
	if (_data->cropping.Top == value) {
		return;
	}

	// 用户已清空数字框则重置为 0
	_data->cropping.Top = std::isnan(value) ? 0.0f : (float)value;
	_propertyChangedEvent(*this, PropertyChangedEventArgs(L"CroppingTop"));

	AppSettings::Get().SaveAsync();
}

double ProfileViewModel::CroppingRight() const noexcept {
	return _data->cropping.Right;
}

void ProfileViewModel::CroppingRight(double value) {
	if (_data->cropping.Right == value) {
		return;
	}

	_data->cropping.Right = std::isnan(value) ? 0.0f : (float)value;
	_propertyChangedEvent(*this, PropertyChangedEventArgs(L"CroppingRight"));

	AppSettings::Get().SaveAsync();
}

double ProfileViewModel::CroppingBottom() const noexcept {
	return _data->cropping.Bottom;
}

void ProfileViewModel::CroppingBottom(double value) {
	if (_data->cropping.Bottom == value) {
		return;
	}

	_data->cropping.Bottom = std::isnan(value) ? 0.0f : (float)value;
	_propertyChangedEvent(*this, PropertyChangedEventArgs(L"CroppingBottom"));

	AppSettings::Get().SaveAsync();
}

bool ProfileViewModel::IsAdjustCursorSpeed() const noexcept {
	return _data->IsAdjustCursorSpeed();
}

void ProfileViewModel::IsAdjustCursorSpeed(bool value) {
	if (_data->IsAdjustCursorSpeed() == value) {
		return;
	}

	_data->IsAdjustCursorSpeed(value);
	_propertyChangedEvent(*this, PropertyChangedEventArgs(L"IsAdjustCursorSpeed"));

	AppSettings::Get().SaveAsync();
}

bool ProfileViewModel::IsDrawCursor() const noexcept {
	return _data->IsDrawCursor();
}

void ProfileViewModel::IsDrawCursor(bool value) {
	if (_data->IsDrawCursor() == value) {
		return;
	}

	_data->IsDrawCursor(value);
	_propertyChangedEvent(*this, PropertyChangedEventArgs(L"IsDrawCursor"));

	AppSettings::Get().SaveAsync();
}

int ProfileViewModel::CursorScaling() const noexcept {
	return (int)_data->cursorScaling;
}

void ProfileViewModel::CursorScaling(int value) {
	if (value < 0) {
		return;
	}

	Magpie::App::CursorScaling cursorScaling = (Magpie::App::CursorScaling)value;
	if (_data->cursorScaling == cursorScaling) {
		return;
	}

	_data->cursorScaling = cursorScaling;
	_propertyChangedEvent(*this, PropertyChangedEventArgs(L"CursorScaling"));

	AppSettings::Get().SaveAsync();
}

double ProfileViewModel::CustomCursorScaling() const noexcept {
	return _data->customCursorScaling;
}

void ProfileViewModel::CustomCursorScaling(double value) {
	if (_data->customCursorScaling == value) {
		return;
	}

	_data->customCursorScaling = std::isnan(value) ? 1.0f : (float)value;
	_propertyChangedEvent(*this, PropertyChangedEventArgs(L"CustomCursorScaling"));

	AppSettings::Get().SaveAsync();
}

int ProfileViewModel::CursorInterpolationMode() const noexcept {
	return (int)_data->cursorInterpolationMode;
}

void ProfileViewModel::CursorInterpolationMode(int value) {
	if (value < 0) {
		return;
	}

	::Magpie::Core::CursorInterpolationMode cursorInterpolationMode = (::Magpie::Core::CursorInterpolationMode)value;
	if (_data->cursorInterpolationMode == cursorInterpolationMode) {
		return;
	}

	_data->cursorInterpolationMode = cursorInterpolationMode;
	_propertyChangedEvent(*this, PropertyChangedEventArgs(L"CursorInterpolationMode"));

	AppSettings::Get().SaveAsync();
}

bool ProfileViewModel::IsDisableDirectFlip() const noexcept {
	return _data->IsDisableDirectFlip();
}

void ProfileViewModel::IsDisableDirectFlip(bool value) {
	if (_data->IsDisableDirectFlip() == value) {
		return;
	}

	_data->IsDisableDirectFlip(value);
	_propertyChangedEvent(*this, PropertyChangedEventArgs(L"IsDisableDirectFlip"));

	AppSettings::Get().SaveAsync();
}

}
